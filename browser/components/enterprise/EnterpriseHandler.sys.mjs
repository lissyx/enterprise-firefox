/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

export const EnterpriseHandler = {
  /**
   * @typedef {object} User
   * @property {string} name name
   * @property {string} email email
   * @property {string} pictureUrl picture url
   */
  _signedInUser: null,
  _document: null,

  async init(window) {
    this._document = window.document;
    this.hideFxaToolbarButton();

    await this.initUser();
    this.updateBadge();
  },

  async initUser() {
    const { ConsoleClient } = ChromeUtils.importESModule(
      "resource:///modules/enterprise/ConsoleClient.sys.mjs"
    );
    try {
      const { name, email, picture } =
        await ConsoleClient.getLoggedInUserInfo();
      this._signedInUser = { name, email, pictureUrl: picture };
    } catch (e) {
      console.error(
        "EnterpriseHandler: Unable to initialize enterprise user: ",
        e
      );
    }
  },

  updateBadge() {
    const userIcon = this._document.querySelector("#enterprise-user-icon");
    userIcon.setProperty(
      "list-style-image",
      `url(${this._signedInUser.pictureURL})`
    );
  },

  openPanel(element, event) {
    this._document.ownerGlobal.PanelUI.showSubView(
      "panelUI-enterprise",
      element,
      event
    );
    const emailSpan = this._document.querySelector(
      ".panelUI-enterprise__email"
    );
    if (!emailSpan.textContent) {
      this._document.querySelector(".panelUI-enterprise__email").textContent =
        this._signedInUser.email;
    }
  },

  /**
   * Hides FxA toolbar button
   * Todo: FxA shows up in a lot of different areas. For now we hide
   *       the most prominent toolbar button. How to hide or integrate
   *       with Fxa and Sync to be determined.
   */
  hideFxaToolbarButton() {
    const fxaBtn = this._document.getElementById("fxa-toolbar-menu-button");
    fxaBtn.hidden = true;
  },

  // TODO: Open signout dialog
  onSignOut() {},
};
