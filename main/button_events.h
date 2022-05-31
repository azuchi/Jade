#ifndef BUTTON_EVENTS_H_
#define BUTTON_EVENTS_H_

typedef enum {
    BTN_EXIT_MESSAGE_SCREEN,

    BTN_YES,
    BTN_NO,

    BTN_TX_SCREEN_NEXT,
    BTN_TX_SCREEN_PREV,
    BTN_TX_SCREEN_EXIT,

    BTN_CANCEL_SIGNATURE,
    BTN_ACCEPT_SIGNATURE,

    BTN_CANCEL_ADDRESS,
    BTN_ACCEPT_ADDRESS,

    BTN_CANCEL_OTA,
    BTN_ACCEPT_OTA,

    BTN_REBOOT,

    BTN_BLE_CONFIRM,
    BTN_BLE_DENY,

    BTN_NEW_MNEMONIC,
    BTN_NEW_MNEMONIC_ADVANCED,

    BTN_NEW_MNEMONIC_12_BEGIN,
    BTN_NEW_MNEMONIC_24_BEGIN,

    BTN_MNEMONIC_PREV,
    BTN_MNEMONIC_NEXT,
    BTN_MNEMONIC_EXIT,
    BTN_MNEMONIC_VERIFY,

    BTN_RECOVER_MNEMONIC,
    BTN_RECOVER_MNEMONIC_ADVANCED,

    BTN_RECOVER_MNEMONIC_12_BEGIN,
    BTN_RECOVER_MNEMONIC_24_BEGIN,

    BTN_QR_MNEMONIC_BEGIN,
    BTN_QR_MNEMONIC_SCAN,
    BTN_QR_MNEMONIC_EXIT,

    BTN_INITIALIZE,
    BTN_SLEEP,
    BTN_SETTINGS,
    BTN_BLE,
    BTN_INFO,

    BTN_CONNECT_BACK,
    BTN_CONNECT_USB,
    BTN_CONNECT_BLE,

    BTN_SETTINGS_TOGGLE_ORIENTATION,
    BTN_SETTINGS_TIMEOUT_0,
    BTN_SETTINGS_TIMEOUT_1,
    BTN_SETTINGS_TIMEOUT_2,
    BTN_SETTINGS_TIMEOUT_3,
    BTN_SETTINGS_TIMEOUT_4,
    BTN_SETTINGS_TIMEOUT_5,
    BTN_SETTINGS_EMERGENCY_RESTORE,
    BTN_SETTINGS_LEGAL,
    BTN_SETTINGS_ADVANCED,
    BTN_SETTINGS_MULTISIG,
    BTN_SETTINGS_RESET,
    BTN_SETTINGS_EXIT,

    BTN_BLE_TOGGLE_ENABLE,
    BTN_BLE_RESET_PAIRING,

    BTN_INFO_SHOW_XPUB,
    BTN_INFO_LEGAL,
    BTN_INFO_STORAGE,
    BTN_INFO_EXIT,

    BTN_LEGAL_NEXT,
    BTN_LEGAL_PREV,

    BTN_BLE_EXIT,

    BTN_PINSERVER_DETAILS_CONFIRM,
    BTN_PINSERVER_DETAILS_DENY,

    BTN_MULTISIG_PREV,
    BTN_MULTISIG_NEXT,
    BTN_MULTISIG_CONFIRM,
    BTN_MULTISIG_DELETE,
    BTN_MULTISIG_EXIT,

    // NOTE: Always leave these ones last as keyboard buttons use
    // BTN_KEYBOARD_ASCII_OFFSET + <ascii-value>
    BTN_KEYBOARD_BACKSPACE,
    BTN_KEYBOARD_ENTER,
    BTN_KEYBOARD_SHIFT,
    BTN_KEYBOARD_ASCII_OFFSET

} button_event_id;

#endif /* BUTTON_EVENTS_H_ */
