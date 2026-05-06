#!/usr/bin/env python
# This Source Code Form is subject to the terms of the Mozilla Public
# License, v. 2.0. If a copy of the MPL was not distributed with this file,
# You can obtain one at http://mozilla.org/MPL/2.0/.

import os
import sys
import time

import requests

# load modules from parent dir
sys.path.insert(1, os.path.dirname(sys.path[0]))

from mozharness.base.errors import BaseErrorList
from mozharness.base.log import INFO
from mozharness.base.vcs.vcsbase import MercurialScript
from mozharness.mozilla.secrets import SecretsMixin
from mozharness.mozilla.structuredlog import StructuredOutputParser
from mozharness.mozilla.testing.errors import HarnessErrorList
from mozharness.mozilla.testing.testbase import TestingMixin
from mozharness.mozilla.testing.unittest import TestSummaryOutputParserHelper


class EnterpriseEnd2EndTest(TestingMixin, MercurialScript, SecretsMixin):
    config_options = ()

    repos = []

    def __init__(self, require_config_file=False):
        super().__init__(
            config_options=self.config_options,
            all_actions=[
                "clobber",
                "get-secrets",
                "run-tests",
            ],
            default_actions=[
                "clobber",
                "get-secrets",
                "run-tests",
            ],
            require_config_file=require_config_file,
            config={"require_test_zip": True},
        )

        # these are necessary since self.config is read only
        c = self.config
        if c.get("structured_output"):
            self.parser_class = StructuredOutputParser
        else:
            self.parser_class = TestSummaryOutputParserHelper

    def query_abs_dirs(self):
        if self.abs_dirs:
            return self.abs_dirs
        abs_dirs = super().query_abs_dirs()
        dirs = {}
        dirs["abs_blob_upload_dir"] = os.path.join(
            abs_dirs["abs_work_dir"], "blobber_upload_dir"
        )

        for key in dirs.keys():
            if key not in abs_dirs:
                abs_dirs[key] = dirs[key]
        self.abs_dirs = abs_dirs
        return self.abs_dirs

    def run_tests(self):
        dirs = self.query_abs_dirs()

        raw_log_file = os.path.join(
            dirs["abs_blob_upload_dir"], "enterprise_end2end_raw.log"
        )
        error_summary_file = os.path.join(
            dirs["abs_blob_upload_dir"], "enterprise_end2end_errorsummary.log"
        )

        config_fmt_args = {
            "raw_log_file": raw_log_file,
            "error_summary_file": error_summary_file,
            "gecko_log": dirs["abs_blob_upload_dir"],
            "this_chunk": self.config.get("this_chunk", 1),
            "total_chunks": self.config.get("total_chunks", 1),
            "repo": self.config.get("repo"),
            "owner": self.config.get("owner"),
            "workflow_id": "EMPTY",
        }

        # python = self.query_python_path("python")
        # cmd = [python, "-u", os.path.join(dirs["abs_enterprise_end2end_dir"], "runtests.py")]

        if self.mkdir_p(dirs["abs_blob_upload_dir"]) == -1:
            # Make sure that the logging directory exists
            self.fatal("Could not create blobber upload directory")

        env = {}
        env["MOZ_UPLOAD_DIR"] = self.query_abs_dirs()["abs_blob_upload_dir"]

        if not os.path.isdir(env["MOZ_UPLOAD_DIR"]):
            self.mkdir_p(env["MOZ_UPLOAD_DIR"])

        env = self.query_env(partial_env=env)

        build_task = os.environ.get("UPSTREAM_TASKIDS", None)
        assert build_task, "There should be an upstream task ID"

        enterprise_end2end_parser = self.parser_class(
            config=self.config,
            log_obj=self.log_obj,
            error_list=BaseErrorList + HarnessErrorList,
            strict=False,
        )

        token = None
        with open("/builds/enterprise-console-backend-apitoken") as token_file:
            token = token_file.read().strip()

        base_url = (
            "https://api.github.com/repos/%(owner)s/%(repo)s/actions" % config_fmt_args
        )
        headers = {
            "Accept": "application/vnd.github+json",
            "Authorization": f"Bearer {token}",
            "X-GitHub-Api-Version": "2026-03-10",
        }

        workflows_url = f"{base_url}/workflows"
        response = requests.get(workflows_url, headers=headers)
        data = response.json()
        if response.status_code != 200:
            self.log(f"GitHub Workflow list failed: {data['message']}")
            raise ValueError(f"github workflow list failed: {response.status_code}")

        data = response.json()
        config_fmt_args["workflow_id"] = next(
            w
            for w in data["workflows"]
            if w["state"] == "active" and w["name"] == "End to End Tests"
        )["id"]

        dispatches_url = (
            base_url + "/workflows/%(workflow_id)s/dispatches" % config_fmt_args
        )
        payload = {
            "ref": "main",
            "inputs": {
                "artifact": f"{build_task}",
                "arch": "x86-64",
            },
        }

        response = requests.post(dispatches_url, headers=headers, json=payload)
        data = response.json()
        if response.status_code != 200:
            self.log(f"GitHub Workflow dispatch failed: {data['message']}")
            raise ValueError(f"github workflow dispatch failed: {response.status_code}")

        run_url = data["run_url"]

        return_code = 1
        while True:
            response = requests.get(run_url, headers=headers)
            data = response.json()

            if data["status"] == "completed":  # adjust condition as needed
                return_code = 0 if data["conclusion"] == "success" else 1
                break

            time.sleep(30)

        level = INFO
        tbpl_status, log_level, summary = enterprise_end2end_parser.evaluate_parser(
            return_code=return_code
        )
        enterprise_end2end_parser.append_tinderboxprint_line("enterprise_end2end")
        enterprise_end2end_parser.print_summary("enterprise_end2end")

        self.log(
            "Enterprise End-to-end exited with return code %s: %s"
            % (return_code, tbpl_status),
            level=level,
        )
        self.record_status(tbpl_status)


if __name__ == "__main__":
    enterpriseEnd2EndTest = EnterpriseEnd2EndTest()
    enterpriseEnd2EndTest.run_and_exit()
