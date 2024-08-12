#include "../button_events.h"
#include "../gui.h"
#include "../jade_assert.h"
#include "../jade_tasks.h"
#include "../process.h"
#include "../serial.h"
#include "../ui.h"
#include "../utils/malloc_ext.h"
#include "usbhmsc.h"
#include <dirent.h>
#include <errno.h>
#include <stdbool.h>
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>

gui_activity_t* make_usb_connect_activity(const char* title);
void await_qr_help_activity(const char* url);

#define MAX_FILENAME_SIZE 256
#define MAX_FILE_ENTRIES 64

static const char FW_SUFFIX[] = "_fw.bin";
static const char HASH_SUFFIX[] = ".hash";

#define STR_ENDSWITH(str, str_len, suffix, suffix_len)                                                                 \
    (str && str_len > suffix_len && str[str_len] == '\0' && !memcmp(str + str_len - suffix_len, suffix, suffix_len))

// Function predicate to filter filenames available for a particular action
typedef bool (*filename_filter_fn_t)(const char* path, const char* filename, const size_t filename_len);

// Fucntion/action to call on a usb-storage directory
typedef bool (*usbstorage_action_fn_t)(const char* extra_path);

// State of usb storage
struct usbstorage_state {
    SemaphoreHandle_t semaphore_usbstorage_event;
    bool usbstorage_mounted;
};

static bool file_exists(const char* file_path)
{
    JADE_ASSERT(file_path);

    struct stat buffer;
    return !stat(file_path, &buffer);
}

static size_t get_file_size(const char* filename)
{
    JADE_ASSERT(filename);

    struct stat st;
    return !stat(filename, &st) ? st.st_size : 0;
}

static size_t read_file_to_buffer(const char* filename, uint8_t* buffer, size_t buf_len)
{
    JADE_ASSERT(filename);
    JADE_ASSERT(buffer);
    JADE_ASSERT(buf_len);

    const size_t file_size = get_file_size(filename);
    JADE_ASSERT(buf_len >= file_size);

    FILE* fp = fopen(filename, "rb");
    if (fp == NULL) {
        return 0;
    }

    const size_t bytes_read = fread(buffer, 1, buf_len, fp);
    fclose(fp);

    JADE_ASSERT(bytes_read == file_size);
    return bytes_read;
}

// Display list of files, and return if user selects one
// Must be passed predicate to filter filenames
// NOTE: 'extra_path' (input) is relative to the mount point, but 'filename' (output) will be the full path including
// the path to the mount point.  eg. path: "/fws" -> filename: "/usb/fws/1.0.31-beta2_ble_1356784_fw.bin"
static bool select_file_from_filtered_list(const char* title, const char* const extra_path, filename_filter_fn_t filter,
    char* filename, const size_t filename_len)
{
    JADE_ASSERT(title);
    // extra_path is optional
    JADE_ASSERT(filter);
    JADE_ASSERT(filename);
    JADE_ASSERT(filename_len == MAX_FILENAME_SIZE);

    char path[MAX_FILENAME_SIZE];
    int ret = snprintf(path, sizeof(path), "%s%s", USBSTORAGE_MOUNT_POINT, extra_path ? extra_path : "");
    JADE_ASSERT(ret > 0 && ret < sizeof(path));
    const size_t path_len = strlen(path);

    DIR* const dir = opendir(path);
    if (dir == NULL) {
        return false;
    }

    char* filenames[MAX_FILE_ENTRIES] = {};
    size_t num_files = 0;

    while (num_files < MAX_FILE_ENTRIES) {
        errno = 0;
        const struct dirent* const entry = readdir(dir);
        if (errno || !entry) {
            break;
        }

        // DT_REG: regular file
        if (entry->d_type == DT_REG) {
            const size_t d_name_len = strlen(entry->d_name);
            if (path_len + 1 + d_name_len + 1 > MAX_FILENAME_SIZE) {
                // We don't support overlong filenames - skip
                continue;
            }

            // Offer to filter function
            if (!filter(path, entry->d_name, d_name_len)) {
                // Does not pass filter - skip
                continue;
            }

            // Add to list of candidate files
            filenames[num_files] = strdup(entry->d_name);
            JADE_ASSERT(filenames[num_files]);
            ++num_files;
        }
    }

    if (closedir(dir) == -1) {
        JADE_LOGE("Error closing directory");
    }

    if (!num_files) {
        // No candidate files
        return false;
    }

    // Display as carousel
    gui_view_node_t* label_item = NULL;
    gui_view_node_t* filename_item = NULL;
    gui_activity_t* const act = make_carousel_activity(title, &label_item, &filename_item);
    JADE_ASSERT(filename_item);
    JADE_ASSERT(label_item);

    uint8_t selected = 0;
    gui_set_align(label_item, GUI_ALIGN_LEFT, GUI_ALIGN_MIDDLE);
    gui_update_text(filename_item, filenames[selected]);
    gui_set_current_activity(act);
    int32_t ev_id;

    char label[32];
    const size_t limit = num_files + 1;
    bool done = false;
    while (!done) {
        JADE_ASSERT(selected <= num_files);
        if (selected < num_files) {
            // File item
            ret = snprintf(label, sizeof(label), "Candidate file %u/%u :", selected + 1, num_files);
            JADE_ASSERT(ret > 0 && ret < sizeof(label));
            gui_update_text(label_item, label);
            gui_update_text(filename_item, filenames[selected]);
        } else {
            // 'Cancel' sentinel
            gui_update_text(label_item, "");
            gui_update_text(filename_item, "[Cancel]");
        }

        if (gui_activity_wait_event(act, GUI_EVENT, ESP_EVENT_ANY_ID, NULL, &ev_id, NULL, 0)) {
            switch (ev_id) {
            case GUI_WHEEL_LEFT_EVENT:
                selected = (selected + limit - 1) % limit;
                break;

            case GUI_WHEEL_RIGHT_EVENT:
                selected = (selected + 1) % limit;
                break;

            default:
                if (ev_id == gui_get_click_event()) {
                    done = true;
                    break;
                }
            }

            if (ev_id == BTN_SETTINGS_USBSTORAGE_HELP) {
                await_qr_help_activity("blkstrm.com/jadeusbstorage");
                gui_set_current_activity(act);
            }
        }
    }

    if (selected < num_files) {
        // Copy the selected filename (including fullpath) to output
        JADE_ASSERT(filenames[selected]);
        ret = snprintf(filename, filename_len, "%s/%s", path, filenames[selected]);
        JADE_ASSERT(ret > 0 && ret < filename_len);
    } else {
        done = false; // ie. cancelled, no filename copied
    }

    // Free the duplicated filename strings
    for (size_t i = 0; i < num_files; ++i) {
        free(filenames[i]);
    }

    return done;
}

// usb storage state event callback
static void handle_usbstorage_event(const usbstorage_event_t event, const uint8_t device_address, void* ctx)
{
    JADE_ASSERT(ctx);

    struct usbstorage_state* const state = (struct usbstorage_state*)ctx;
    JADE_ASSERT(state->semaphore_usbstorage_event);

    // When the device is detected, mount it immediately
    if (event == USBSTORAGE_DETECTED) {
        if (usbstorage_mount(device_address)) {
            state->usbstorage_mounted = true;
            xSemaphoreGive(state->semaphore_usbstorage_event);
        } else {
            JADE_LOGE("Failed to mount USB storage!");
            const char* message[] = { "Error accessing", "usb storage!" };
            await_error_activity(message, 2);
        }
    }

    // Handle other events ?
}

// Generic handler to run usb storage actions
static bool handle_usbstorage_action(
    const char* title, usbstorage_action_fn_t usbstorage_action, const char* extra_path, const bool async_action)
{
    JADE_ASSERT(title);
    JADE_ASSERT(usbstorage_action);
    // extra_path is optional

    // Stop normal serial usb and start usbstorage
    display_processing_message_activity();
    serial_stop();

    struct usbstorage_state state = { .usbstorage_mounted = false };
    state.semaphore_usbstorage_event = xSemaphoreCreateBinary();
    JADE_ASSERT(state.semaphore_usbstorage_event);
    usbstorage_register_callback(handle_usbstorage_event, &state);

    if (!usbstorage_start()) {
        JADE_LOGE("Failed to start USB storage!");
        const char* message[] = { "Failed to start", "usb storage!" };
        await_error_activity(message, 2);

        // !! Jade may require restart though? !!
        JADE_ASSERT_MSG(false, "usbstorage_start failed");
    }

    // We should only do this if within 0.4 seconds or so we don't detect a usb device already plugged

    // Now wait for either the the sempahore to be unlocked or for back button on the activity
    size_t counter = 0;
    gui_activity_t* act_prompt = NULL;
    bool action_initiated = false;
    while (true) {
        // If the usb_storage is mounted, run the action
        if (state.usbstorage_mounted) {
            action_initiated = usbstorage_action(extra_path);
            break;
        }

        // If the usb-storage device is not detected/mounted, show a screen prompting the user
        if (!act_prompt && !state.usbstorage_mounted) {
            xSemaphoreTake(state.semaphore_usbstorage_event, 200 / portTICK_PERIOD_MS);
            if (!state.usbstorage_mounted) {
                if (counter < 5) {
                    ++counter;
                    continue;
                }

                // Prompt user to plug usbstorage device
                act_prompt = make_usb_connect_activity(title);
                gui_set_current_activity(act_prompt);
            }
        }

        // Handle any events from that screen
        int32_t ev_id;
        if (act_prompt
            && gui_activity_wait_event(
                act_prompt, GUI_BUTTON_EVENT, ESP_EVENT_ANY_ID, NULL, &ev_id, NULL, 100 / portTICK_PERIOD_MS)) {

            if (ev_id == BTN_SETTINGS_USBSTORAGE_BACK) {
                usbstorage_register_callback(NULL, NULL);
                // when the user goes back we go through here
                // then the device hasn't started any action, but has disk detected
                break;
            }
            if (ev_id == BTN_SETTINGS_USBSTORAGE_HELP) {
                await_qr_help_activity("blkstrm.com/jadeusbstorage");
                gui_set_current_activity(act_prompt);
            }
        }
    }

    // If the action was not an async action (ie. it has already completed) or
    // the action was never properly started, we stop/unmount usbstorage now.
    if (!async_action || !action_initiated) {
        if (state.usbstorage_mounted) {
            // if it was detected it was mounted
            usbstorage_unmount();
        }
        usbstorage_stop();
        serial_start();
    }

    usbstorage_register_callback(NULL, NULL);
    vSemaphoreDelete(state.semaphore_usbstorage_event);

    return action_initiated;
}

// OTA

static void prepare_common_msg(CborEncoder* root_map_encoder, CborEncoder* root_encoder, const jade_msg_source_t source,
    const char* method, uint8_t* buffer, const size_t buffer_len, const bool has_params)
{
    JADE_ASSERT(root_map_encoder);
    JADE_ASSERT(root_encoder);
    JADE_ASSERT(method);
    JADE_ASSERT(buffer);
    JADE_ASSERT(buffer_len);

    cbor_encoder_init(root_encoder, buffer, buffer_len, 0);
    const CborError cberr = cbor_encoder_create_map(root_encoder, root_map_encoder, has_params ? 3 : 2);
    JADE_ASSERT(cberr == CborNoError);
    add_string_to_map(root_map_encoder, "id", "0");
    add_string_to_map(root_map_encoder, "method", method);
}

static bool post_ota_message(const jade_msg_source_t source, const size_t fwsize, const size_t cmpsize,
    const uint8_t* hash, const size_t hash_len)
{
    JADE_ASSERT(hash);
    JADE_ASSERT(hash_len == SHA256_LEN);

    CborEncoder root_encoder;
    CborEncoder root_map_encoder;

    // FIXME: check max size required?
    uint8_t cbor_buf[512 + 128];
    const bool has_params = true;
    prepare_common_msg(&root_map_encoder, &root_encoder, source, "ota", cbor_buf, sizeof(cbor_buf), has_params);

    CborError cberr = cbor_encode_text_stringz(&root_map_encoder, "params");
    JADE_ASSERT(cberr == CborNoError);

    CborEncoder params_encoder; // fwsize/cmpsize/fwhash
    cberr = cbor_encoder_create_map(&root_map_encoder, &params_encoder, 3);
    JADE_ASSERT(cberr == CborNoError);
    add_uint_to_map(&params_encoder, "fwsize", fwsize);
    add_uint_to_map(&params_encoder, "cmpsize", cmpsize);
    add_bytes_to_map(&params_encoder, "fwhash", hash, hash_len);

    cberr = cbor_encoder_close_container(&root_map_encoder, &params_encoder);
    JADE_ASSERT(cberr == CborNoError);

    cberr = cbor_encoder_close_container(&root_encoder, &root_map_encoder);
    JADE_ASSERT(cberr == CborNoError);

    const size_t cbor_len = cbor_encoder_get_buffer_size(&root_encoder, cbor_buf);
    return jade_process_push_in_message_ex(cbor_buf, cbor_len, source);
}

static bool post_ota_data_message(const jade_msg_source_t source, uint8_t* data, size_t data_len)
{
    JADE_ASSERT(data);
    JADE_ASSERT(data_len);

    // FIXME: check max size required?
    uint8_t cbor_buf[4096 + 128];
    CborEncoder root_encoder;
    CborEncoder root_map_encoder;

    const bool has_params = true;
    prepare_common_msg(&root_map_encoder, &root_encoder, source, "ota_data", cbor_buf, sizeof(cbor_buf), has_params);
    add_bytes_to_map(&root_map_encoder, "params", data, data_len);

    const CborError cberr = cbor_encoder_close_container(&root_encoder, &root_map_encoder);
    JADE_ASSERT(cberr == CborNoError);

    const size_t cbor_len = cbor_encoder_get_buffer_size(&root_encoder, cbor_buf);
    return jade_process_push_in_message_ex(cbor_buf, cbor_len, source);
}

static bool post_ota_complete_message(const jade_msg_source_t source)
{
    // FIXME: check max size required?
    uint8_t cbor_buf[64];
    CborEncoder root_encoder;
    CborEncoder root_map_encoder; // id, method
    const bool has_params = false;
    prepare_common_msg(
        &root_map_encoder, &root_encoder, source, "ota_complete", cbor_buf, sizeof(cbor_buf), has_params);
    const CborError cberr = cbor_encoder_close_container(&root_encoder, &root_map_encoder);
    JADE_ASSERT(cberr == CborNoError);

    const size_t cbor_len = cbor_encoder_get_buffer_size(&root_encoder, cbor_buf);
    return jade_process_push_in_message_ex(cbor_buf, cbor_len, source);
}

#define MAX_FW_SIZE_DIGITS 7

static size_t read_fwsize(const char* str)
{
    JADE_ASSERT(str);

    const char* last_underscore = strrchr(str, '_');
    if (!last_underscore || *(last_underscore + 1) == '\0') {
        return 0;
    }

    const char* second_last_underscore = last_underscore - 1;
    while (second_last_underscore >= str) {
        if (*second_last_underscore == '_') {
            break;
        }
        --second_last_underscore;
    }

    if (second_last_underscore < str) {
        return 0;
    }

    const char* start = second_last_underscore + 1;
    const char* end = last_underscore;
    if (end == start || (end - start) > MAX_FW_SIZE_DIGITS) {
        return 0;
    }

    char temp[MAX_FW_SIZE_DIGITS + 1]; // Maximum digits plus null terminator
    strncpy(temp, start, end - start);
    temp[end - start] = '\0';

    return strtoul(temp, NULL, 10);
}

static bool read_hash_file_to_buffer(const char* filename, uint8_t* buffer, size_t buf_size)
{
    JADE_ASSERT(filename);
    JADE_ASSERT(buffer);
    JADE_ASSERT(buf_size == SHA256_LEN);

    char hash_hex[SHA256_LEN * 2];
    struct stat st;
    if (stat(filename, &st) != 0 || st.st_size != 64) {
        return false;
    }

    const size_t bytes_read = read_file_to_buffer(filename, (uint8_t*)hash_hex, sizeof(hash_hex));
    if (bytes_read != 64) {
        return false;
    }

    size_t written = 0;
    const int wally_res = wally_hex_n_to_bytes(hash_hex, sizeof(hash_hex), buffer, buf_size, &written);
    JADE_ASSERT(written == SHA256_LEN);
    JADE_ASSERT(wally_res == WALLY_OK);
    return true;
}

static bool handle_ota_reply(const uint8_t* msg, const size_t len, void* ctx)
{
    JADE_ASSERT(msg);
    JADE_ASSERT(len);
    JADE_ASSERT(ctx);

    bool* const ok = (bool*)ctx;
    *ok = false;

    CborParser parser;
    CborValue message;
    const CborError cberr = cbor_parser_init(msg, len, CborValidateBasic, &parser, &message);
    if (cberr != CborNoError || !rpc_message_valid(&message)) {
        JADE_LOGE("Invalid cbor message");
        goto cleanup;
    }

    bool bool_result = false;
    if (!rpc_get_boolean("result", &message, &bool_result)) {
        goto cleanup;
    }

    *ok = bool_result;

cleanup:
    // We return true in all cases to indicate that a message was received
    // and we should stop waiting - whether the message was processed 'successfully'
    // is indicated by the 'ok' flag in the passed context object.
    return true;
}

struct usbmode_ota_worker_data {
    char* file_to_flash;
    size_t data_to_send;
};

static void usbmode_ota_worker(void* ctx)
{
    JADE_ASSERT(ctx);
    struct usbmode_ota_worker_data* ctx_data = (struct usbmode_ota_worker_data*)ctx;
    JADE_ASSERT(ctx_data->file_to_flash);

    uint8_t buffer[4096];

    FILE* fp = fopen(ctx_data->file_to_flash, "rb");
    if (fp == NULL) {
        // FIXME: handle better? this happens if the user unplugged the device for example
        free(ctx_data->file_to_flash);
        vTaskDelete(NULL);
        return;
    }

    // first we send data packets and for each we wait for an ok
    bool ok;
    while (ctx_data->data_to_send) {
        ok = false;
        while (!jade_process_get_out_message(handle_ota_reply, SOURCE_INTERNAL, &ok)) {
            // Await outbound message
        }

        if (!ok) {
            /* user rejected the firmware most likely */
            break;
        }

        const size_t written = fread(buffer, 1, sizeof(buffer), fp);
        if (!written) {
            // This happens if the user unplugs the device in the middle of ota
            // at the moment we fail gracefully but we need to send another ota message for things to get unstuck on the
            // firmware side of things sending an ota complete should do the job
            // FIXME: instead create an ota_cancel msg and send that (and add support to ota for that)
            post_ota_complete_message(SOURCE_INTERNAL);
            // then we wait for the reply back
            ok = false;
            while (!jade_process_get_out_message(handle_ota_reply, SOURCE_INTERNAL, &ok)) {
                // Await outbound message
            }
            break;
        }
        const bool res = post_ota_data_message(SOURCE_INTERNAL, buffer, written);
        JADE_ASSERT(res);
        ctx_data->data_to_send -= written;
    }

    if (!fclose(fp)) {
        JADE_LOGE("Closing file %s failed", ctx_data->file_to_flash);
    }

    free(ctx_data->file_to_flash);
    const size_t data_to_send = ctx_data->data_to_send;
    free(ctx_data);

    if (!data_to_send) {
        // all data sent, proceed with ota_complete message
        post_ota_complete_message(SOURCE_INTERNAL);
        ok = false;

        while (!jade_process_get_out_message(handle_ota_reply, SOURCE_INTERNAL, &ok)) {
            // Await outbound message
        }
        // ota success, device will be rebooted soon
    }

    // After ota try to unmount usbstorage and restart normal serial comms
    usbstorage_unmount();
    usbstorage_stop();
    serial_start();

    vTaskDelete(NULL);
}

static void start_usb_ota_task(char* str, size_t fwsize, size_t cmpsize, uint8_t* hash, const size_t hash_len)
{
    JADE_ASSERT(str);
    JADE_ASSERT(fwsize);
    JADE_ASSERT(cmpsize);
    JADE_ASSERT(hash);
    JADE_ASSERT(hash_len == SHA256_LEN);

    const bool res = post_ota_message(SOURCE_INTERNAL, fwsize, cmpsize, hash, hash_len);
    JADE_ASSERT(res);

    // FIXME: check stack size better
    char* copy = strdup(str);
    JADE_ASSERT(copy);
    struct usbmode_ota_worker_data* ctx_data = JADE_MALLOC(sizeof(struct usbmode_ota_worker_data));
    JADE_ASSERT(ctx_data);

    ctx_data->file_to_flash = copy;
    ctx_data->data_to_send = cmpsize;

    const BaseType_t retval = xTaskCreatePinnedToCore(
        usbmode_ota_worker, "usb_ota_task", 10 * 1024, ctx_data, JADE_TASK_PRIO_USB, NULL, JADE_CORE_SECONDARY);
    JADE_ASSERT_MSG(retval == pdPASS, "Failed to create usb_ota_task, xTaskCreatePinnedToCore() returned %d", retval);
}

static bool is_full_fw_file(const char* path, const char* filename, const size_t filename_len)
{
    // Must look like a fw file (name)
    if (!STR_ENDSWITH(filename, filename_len, FW_SUFFIX, strlen(FW_SUFFIX))) {
        return false;
    }

    // Must have corresponding hash file
    char hash_filename[MAX_FILENAME_SIZE];
    if (strlen(path) + 1 + filename_len + sizeof(HASH_SUFFIX) > sizeof(hash_filename)) {
        return false;
    }
    const int ret = snprintf(hash_filename, sizeof(hash_filename), "%s/%s%s", path, filename, HASH_SUFFIX);
    JADE_ASSERT(ret > 0 && ret < sizeof(hash_filename));

    return file_exists(hash_filename);
}

static bool initiate_usb_ota(const char* extra_path)
{
    // extra_path is optional

    char filename[MAX_FILENAME_SIZE];
    if (!select_file_from_filtered_list("Select Firmware", extra_path, is_full_fw_file, filename, sizeof(filename))) {
        // User abandoned
        return false;
    }

    char hash_filename[MAX_FILENAME_SIZE];
    const int ret = snprintf(hash_filename, sizeof(hash_filename), "%s%s", filename, HASH_SUFFIX);
    JADE_ASSERT(ret > 0 && ret < sizeof(hash_filename));

    uint8_t hash[SHA256_LEN];
    if (!read_hash_file_to_buffer(hash_filename, hash, sizeof(hash))) {
        const char* message[] = { "Failed to read", "hash file" };
        await_error_activity(message, 2);
        return false;
    }

    const size_t cmpsize = get_file_size(filename);
    const size_t fwsize = read_fwsize(filename);
    if (!cmpsize || !fwsize) {
        const char* message[] = { "Failed to parse", "firmware filename" };
        await_error_activity(message, 2);
        return false;
    }

    start_usb_ota_task(filename, fwsize, cmpsize, hash, sizeof(hash));
    return true;
}

// Initiate an OTA fw upgrade from compressed fw and hash file
// NOTE: this function starts a separate task to read the file and provide the fw chunks.
// It must return and pass control back to the main dispatcher loop to process the OTA.
bool usbstorage_firmware_ota(const char* extra_path)
{
    // extra_path is optional
    const bool is_async = true;
    return handle_usbstorage_action("Firmware Upgrade", initiate_usb_ota, extra_path, is_async);
}
