# This Source Code Form is subject to the terms of the Mozilla Public
# License, v. 2.0. If a copy of the MPL was not distributed with this
# file, You can obtain one at http://mozilla.org/MPL/2.0/.

import pytest
from mozunit import main
from taskgraph.util import json

from gecko_taskgraph.test.conftest import FakeParameters
from gecko_taskgraph.transforms.comm_decision import (
    add_comm_checkout,
    set_decision_command,
)

PUSH_PARAMS = {
    "base_repository": "https://github.com/mozilla/enterprise-firefox",
    "base_rev": "d1cc0b26a4e5e8be4dbec8393f1c34bc9b7cbfaf",
    "head_repository": "https://github.com/mozilla/enterprise-firefox",
    "head_ref": "refs/heads/enterprise-beta",
    "head_rev": "7b2d3ff6b1d3b6ab5c1a4e2d5e8de5e2a0d6c5b4",
    "level": "3",
    "owner": "someone@mozilla.com",
    "project": "enterprise-firefox",
    "tasks_for": "github-push",
}

PULL_REQUEST_PARAMS = {
    **PUSH_PARAMS,
    "head_repository": "https://github.com/someone/enterprise-firefox",
    "head_ref": "refs/heads/a-branch",
    "level": "1",
    "pull_request_number": 42,
    "tasks_for": "github-pull-request",
}

TRY_PARAMS = {
    **PUSH_PARAMS,
    "base_repository": "https://github.com/mozilla/enterprise-firefox-try",
    "head_repository": "https://github.com/mozilla/enterprise-firefox-try",
    "head_ref": "refs/heads/a-try-push",
    "level": "1",
    "project": "enterprise-firefox-try",
}


def run(run_transform, xform, task, params):
    tasks = list(run_transform(xform, [task], params=FakeParameters(params)))
    assert len(tasks) == 1
    return tasks[0]


@pytest.mark.parametrize(
    "params,expected_project,expected_scope,expected_routes",
    (
        pytest.param(
            PUSH_PARAMS,
            "enterprise-thunderbird",
            "assume:repo:github.com/mozilla/enterprise-firefox:branch:enterprise-beta",
            [
                "index.test-domain.v2.enterprise-firefox.revision"
                ".7b2d3ff6b1d3b6ab5c1a4e2d5e8de5e2a0d6c5b4.taskgraph.comm-decision",
                "index.test-domain.v2.enterprise-firefox.branch.enterprise-beta"
                ".latest.taskgraph.comm-decision",
            ],
            id="push",
        ),
        pytest.param(
            PULL_REQUEST_PARAMS,
            "enterprise-thunderbird",
            "assume:repo:github.com/mozilla/enterprise-firefox:pull-request",
            [
                "index.test-domain.v2.enterprise-firefox-pr.revision"
                ".7b2d3ff6b1d3b6ab5c1a4e2d5e8de5e2a0d6c5b4.taskgraph.comm-decision",
            ],
            id="pull-request",
        ),
        pytest.param(
            TRY_PARAMS,
            "enterprise-thunderbird-try",
            "assume:repo:github.com/mozilla/enterprise-firefox-try:branch:a-try-push",
            [
                "index.test-domain.v2.enterprise-firefox-try.revision"
                ".7b2d3ff6b1d3b6ab5c1a4e2d5e8de5e2a0d6c5b4.taskgraph.comm-decision",
                "index.test-domain.v2.enterprise-firefox-try.branch.a-try-push"
                ".latest.taskgraph.comm-decision",
            ],
            id="try",
        ),
    ),
)
def test_decision_command(
    run_transform, params, expected_project, expected_scope, expected_routes
):
    task = run(run_transform, set_decision_command, {"run": {}}, params)

    command = task["run"]["command"]
    assert "--root=comm/taskcluster" in command
    assert f"--project='{expected_project}'" in command
    assert f"--head-rev='{params['head_rev']}'" in command
    assert ("--allow-parameter-override" in command) == (
        params["project"] != "enterprise-firefox"
    )
    assert task["scopes"] == [expected_scope]
    assert task["routes"] == expected_routes
    assert task["extra"]["tasks_for"] == params["tasks_for"]


@pytest.mark.parametrize(
    "params,expected_ref",
    (
        pytest.param(PUSH_PARAMS, "refs/heads/beta", id="push"),
        pytest.param(PULL_REQUEST_PARAMS, "refs/heads/main", id="pull-request"),
        pytest.param(TRY_PARAMS, "refs/heads/main", id="try"),
    ),
)
def test_comm_checkout(run_transform, params, expected_ref):
    task = run(
        run_transform,
        add_comm_checkout,
        {
            "worker": {
                "env": {
                    "GECKO_PATH": "/builds/worker/checkouts/gecko",
                    "REPOSITORIES": json.dumps({"gecko": "Mozilla Firefox"}),
                },
                "command": [
                    "/builds/worker/bin/run-task-git",
                    "--gecko-checkout=/builds/worker/checkouts/gecko",
                    "--",
                    "bash",
                    "-cx",
                    "true",
                ],
            }
        },
        params,
    )

    env = task["worker"]["env"]
    assert env["COMM_HEAD_REF"] == expected_ref
    assert env["COMM_BASE_REF"] == expected_ref
    assert env["COMM_REPOSITORY_TYPE"] == "git"
    assert (
        env["COMM_HEAD_REPOSITORY"]
        == "https://github.com/thunderbird/thunderbird-desktop"
    )
    assert env["COMM_BASE_REV"] == env["COMM_HEAD_REV"]
    assert json.loads(env["REPOSITORIES"]) == {
        "gecko": "Mozilla Firefox",
        "comm": "Mozilla Thunderbird",
    }
    assert task["worker"]["command"][2:4] == [
        "--comm-checkout=/builds/worker/checkouts/gecko/comm",
        "--comm-shallow-clone",
    ]


if __name__ == "__main__":
    main()
