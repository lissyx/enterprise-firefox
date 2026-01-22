#!/usr/bin/env python3
# This Source Code Form is subject to the terms of the Mozilla Public
# License, v. 2.0. If a copy of the MPL was not distributed with this
# file, You can obtain one at https://mozilla.org/MPL/2.0/.


from felt_tests import FeltTests


class FeltStartsBrowser(FeltTests):
    def run_felt_browser_started(self):
        self.connect_child_browser()
        self.open_tab_child(f"http://localhost:{self.sso_port}/sso_page")

        expected_cookie = list(
            filter(
                lambda x: x["name"] == self.cookie_name.value
                and x["value"] == self.cookie_value.value,
                self._child_driver.get_cookies(),
            )
        )
        assert len(expected_cookie) == 1, (
            f"Cookie {self.cookie_name} was properly set on Firefox started by FELT"
        )

    def run_felt_verify_prefs(self):
        prefs = [
          ["browser.sessionstore.restore_on_demand", False, "Bool"],
          ["browser.sessionstore.resume_from_crash", False, "Bool"],
          ["browser.policies.live_polling.frequency", 500, "Int"],
          ["devtools.browsertoolbox.scope", "everything", "String"],
          ["enterprise.console.test_float", 1.5, "Float"],
          ["enterprise.console.test_bool", True, "Bool"]
        ]

        for pref in prefs:
            value = self.get_pref_child(pref[0], pref[2])
            assert value == pref[1], (
                f"Mismatching pref {pref[0]} value {value} instead of {pref[1]}"
            )
