# This Source Code Form is subject to the terms of the Mozilla Public
# License, v. 2.0. If a copy of the MPL was not distributed with this
# file, You can obtain one at http://mozilla.org/MPL/2.0/.
"""
Build the Thunderbird decision task, which generates the comm task graph out of
the thunderbird-desktop tree that goes with this Firefox branch.

The comm checkout is wired up here rather than through ``run.comm-checkout``,
because that one reads the ``comm_*`` parameters, which belong to the comm
parameter set and do not exist in this graph.
"""

import functools

from taskgraph.transforms.base import TransformSequence
from taskgraph.util import json
from taskgraph.util.keyed_by import evaluate_keyed_by
from taskgraph.util.yaml import load_yaml

from gecko_taskgraph import GECKO

# Which thunderbird-desktop tree to build, see the file itself.
COMM_REV_FILE = "comm_rev.yml"

COMM_PROJECT_BY_PROJECT = {
    "enterprise-firefox-try": "enterprise-thunderbird-try",
}
COMM_DEFAULT_PROJECT = "enterprise-thunderbird"

# comm/taskcluster/config.yml is Thunderbird's, and carries Thunderbird's trust
# domain and scope prefix, which are not the ones this graph runs under.
COMM_CONFIG = "comm/taskcluster/config.yml"
COMM_TRUST_DOMAIN = "enterprise"
COMM_SCOPE_PREFIX = "project:enterprise:releng"

# Repositories that are allowed to build a graph with overridden parameters.
PARAMETER_OVERRIDE_PROJECTS = (
    "enterprise-firefox-try",
    "firefox-dev",
    "staging-firefox",
)

decision_command = TransformSequence()
comm_checkout = TransformSequence()


@functools.cache
def _comm_rev():
    return load_yaml(GECKO, COMM_REV_FILE)


def _is_pull_request(params):
    return params["tasks_for"].startswith("github-pull-request")


def _branch(params):
    return (params["head_ref"] or "").removeprefix("refs/heads/")


def _comm_env(params):
    """The environment run-task checks the comm repository out from."""
    env = {
        key: evaluate_keyed_by(
            value, COMM_REV_FILE, {"head-ref": params["head_ref"] or ""}
        )
        for key, value in _comm_rev().items()
    }
    env["COMM_REPOSITORY_TYPE"] = "git"

    # comm has no notion of a base different from its head: this graph is not
    # built out of a comm push, it picks a comm revision up. Both are still
    # needed, by the clone run-task seeds from the base and by the comm decision
    # task, which turns them into its own parameters.
    env["COMM_BASE_REF"] = env["COMM_HEAD_REF"]
    if rev := env.get("COMM_HEAD_REV"):
        env["COMM_BASE_REV"] = rev

    return env


def _comm_project(params):
    return COMM_PROJECT_BY_PROJECT.get(params["project"], COMM_DEFAULT_PROJECT)


def _repo_scope(params):
    """The role the Firefox decision task holds, and hands over here.

    Creating the Thunderbird graph needs every scope its tasks use, which is
    what this role grants. It has to be the very role of the push this graph
    belongs to, since that is the only one the Firefox decision task can pass
    on.
    """
    if _is_pull_request(params):
        repository = params["base_repository"].split("://", 1)[-1]
        return f"assume:repo:{repository}:pull-request"

    repository = params["head_repository"].split("://", 1)[-1]
    return f"assume:repo:{repository}:branch:{_branch(params)}"


def _index_routes(config):
    """Where to find this task, next to the Firefox decision task of the push."""
    params = config.params
    namespace = "{}.v2.{}".format(
        config.graph_config["trust-domain"], params["project"]
    )
    if _is_pull_request(params):
        return [
            f"index.{namespace}-pr.revision.{params['head_rev']}.taskgraph.comm-decision"
        ]

    routes = [
        f"index.{namespace}.revision.{params['head_rev']}.taskgraph.comm-decision"
    ]
    if branch := _branch(params):
        routes.append(
            f"index.{namespace}.branch.{branch}.latest.taskgraph.comm-decision"
        )
    return routes


def _command(params):
    """Generate the comm task graph out of the checkouts of this task."""
    decision = [
        "./mach --log-no-times taskgraph decision",
        "--root=comm/taskcluster",
        "--pushlog-id='0'",
        "--pushdate='0'",
        f"--project='{_comm_project(params)}'",
        f"--owner='{params['owner']}'",
        f"--level='{params['level']}'",
        "--repository-type=git",
        f"--tasks-for='{params['tasks_for']}'",
        f"--base-repository='{params['base_repository']}'",
        f"--base-rev='{params['base_rev']}'",
        f"--head-repository='{params['head_repository']}'",
        f"--head-ref='{params['head_ref']}'",
        f"--head-rev='{params['head_rev']}'",
    ]
    if params["project"] in PARAMETER_OVERRIDE_PROJECTS:
        decision.append("--allow-parameter-override")

    return " && ".join([
        # `mach taskgraph` writes its artifacts under the checkout.
        "ln -s /builds/worker/artifacts artifacts",
        f"sed -i 's|^trust-domain: .*|trust-domain: {COMM_TRUST_DOMAIN}|g' {COMM_CONFIG}",
        "sed -i 's|^    scope-prefix: .*|    scope-prefix:"
        f" {COMM_SCOPE_PREFIX}|g' {COMM_CONFIG}",
        " ".join(decision),
    ])


@decision_command.add
def set_decision_command(config, tasks):
    """Set up what makes this task the decision task of the Thunderbird graph."""
    for task in tasks:
        task["run"]["command"] = _command(config.params)
        task.setdefault("scopes", []).append(_repo_scope(config.params))
        task.setdefault("routes", []).extend(_index_routes(config))
        # Scriptworker walks the chain of trust of a Thunderbird task up to
        # this one, and reads the event that started it all off here, the way
        # every other decision task exposes it.
        task.setdefault("extra", {})["tasks_for"] = config.params["tasks_for"]
        yield task


@comm_checkout.add
def add_comm_checkout(config, tasks):
    params = config.params

    for task in tasks:
        env = task["worker"]["env"]
        env.update(_comm_env(params))
        if params.get("pull_request_number"):
            env["GECKO_PULL_REQUEST_NUMBER"] = str(params["pull_request_number"])

        # REPOSITORIES is built out of the repositories of this graph config, so
        # comm has to be added back once the checkout has been set up.
        repositories = json.loads(env["REPOSITORIES"])
        repositories["comm"] = "Mozilla Thunderbird"
        env["REPOSITORIES"] = json.dumps(repositories)

        command = task["worker"]["command"]
        separator = command.index("--")
        command[separator:separator] = [
            "--comm-checkout={}/comm".format(env["GECKO_PATH"]),
            "--comm-shallow-clone",
        ]

        yield task
