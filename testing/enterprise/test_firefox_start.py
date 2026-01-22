#!/usr/bin/env python3
# This Source Code Form is subject to the terms of the Mozilla Public
# License, v. 2.0. If a copy of the MPL was not distributed with this
# file, You can obtain one at https://mozilla.org/MPL/2.0/.

import os
import sys

sys.path.append(os.path.dirname(__file__))

from base_test import EnterpriseTestsBase
from marionette_driver import expected
from marionette_driver.by import By
from marionette_driver.wait import Wait


class EnterpriseTests(EnterpriseTestsBase):
    EXTRA_ENV = {
        "MOZ_BYPASS_FELT": "1",
        "MOZ_AUTOMATION": "1",
    }

    def test_firefox_start(self): 
        self.open_tab("about:support")
        version_box = self._driver.find_element(By.ID, "version-box")
        Wait(self._driver, 10).until(expected.element_displayed(version_box))
        Wait(self._driver, 10).until(lambda d: len(version_box.text) > 0)
        self._logger.info(f"about:support version: {version_box.text}")
        expected_version = self.marionette.session_capabilities.get("browserVersion")
        self._logger.info(f"expected version: {expected_version}")
        assert version_box.text == expected_version, "version text should match"

        self.open_tab("about:buildconfig")
        build_flags_box = self._driver.find_element(By.CSS_SELECTOR, "p:last-child")
        Wait(self._driver, 10).until(expected.element_displayed(build_flags_box))
        Wait(self._driver, 10).until(lambda d: len(build_flags_box.text) > 0)
        self._logger.info(f"about:buildconfig buildflags: {build_flags_box.text}")
        assert "--enable-enterprise" in build_flags_box.text, (
           "enterprise build flag should be there"
        )
