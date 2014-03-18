#include <stdio.h>
#include <string.h>
#include <stdlib.h>

#include <libimobiledevice/libimobiledevice.h>
#include <libimobiledevice/lockdown.h>
#include <libimobiledevice/installation_proxy.h>
#include <libimobiledevice/afc.h>

#include "windows.h"

char tohex(int x)
{
    static char* hexchars = "0123456789ABCDEF";
    return hexchars[x];
}

unsigned int fromhex(char c)
{
    if (c >= '0' && c <= '9')
        return c - '0';
    else if (c >= 'a' && c <= 'f')
        return 10 + c - 'a';
    else if (c >= 'A' && c <= 'F')
        return 10 + c - 'A';
    return 0;
}

char *hex2strn(const char *data, const int len)
{
    if (len == 0) {
        return NULL;
    }
    
    int result_len = len/2 + 1;

    char *result = (char *)malloc((result_len + 1) * sizeof(char));

    int i=0;
    int j=0;
    while(data[i] != '#' && i<len) {
        result[j++] = fromhex(data[i]) << 4 | fromhex(data[i+1]);
        i+=2;
    }
    result[j] = 0x00;
    return result;
}

char *hex2str(const char *data)
{
    return hex2strn(data, strlen(data));
}

void send_str(char* buf, idevice_connection_t connection)
{
    int bytes = 0;
    idevice_connection_send(connection, buf, strlen(buf), (uint32_t*)&bytes);
}

void recv_pkt(idevice_connection_t connection)
{
    uint32_t bytes = 0;
    char buf[16*1024];
    idevice_connection_receive_timeout(connection, buf, sizeof(buf)-1, &bytes, 1000);
    if (bytes >= 0)
        buf[bytes] = 0x00;
    if (bytes > 0 && buf[0] == '$') {
        send_str("+", connection);
        if (bytes > 1 && buf[1] == 'O') {
            char* c = buf+2;
            char *buf3 = hex2str(c);
            if (strlen(buf3) > 10) {
                printf("%s", buf3);
            }
            fflush(stderr);
            fflush(stdout);
        }
    }
}

void send_pkt(char* buf, idevice_connection_t connection)
{
    int i;
    unsigned char csum = 0;
    char *buf2 =  (char *)malloc (32*1024);
    int cnt = strlen (buf);
    char *p;

    /* Copy the packet into buffer BUF2, encapsulating it
       and giving it a checksum.  */

    p = buf2;
    *p++ = '$';

    for (i = 0; i < cnt; i++)
    {
        csum += buf[i];
        *p++ = buf[i];
    }
    *p++ = '#';
    *p++ = tohex ((csum >> 4) & 0xf);
    *p++ = tohex (csum & 0xf);

    *p = '\0';

    int bytes = 0;
    idevice_connection_send(connection, buf2, strlen(buf2), (uint32_t*)&bytes);
    free(buf2);

    recv_pkt(connection);
}

int op_completed = 0;
int err_occured = 0;
static void status_cb(const char *operation, plist_t status, void *unused) {
    if (status && operation) {
        plist_t npercent = plist_dict_get_item(status, "PercentComplete");
        plist_t nstatus = plist_dict_get_item(status, "Status");
        plist_t nerror = plist_dict_get_item(status, "Error");

        uint64_t percent = 0;
        char *status_msg = NULL;
        if (npercent) {
            uint64_t val = 0;
            plist_get_uint_val(npercent, &val);
            percent = val;
        }
        if (nstatus) {
            plist_get_string_val(nstatus, &status_msg);
            if (!strcmp(status_msg, "Complete")) {
                op_completed = 1;
            }
        }
        if (!nerror) {
            if (!npercent) {
                printf("%s - %s\n", operation, status_msg);
            } else {
                printf("%s - %s (%d%%)\n", operation, status_msg, percent);
            }
        } else {
            char *err_msg = NULL;
            plist_get_string_val(nerror, &err_msg);
            printf("%s - Error occured: %s\n", operation, err_msg);
            err_occured = 1;
        }
    }
}

static void print_usage(int argc, char **argv)
{
    char *name = NULL;
    
    name = strrchr(argv[0], '/');
    printf("Usage: %s [UUID] IPA BundleIdentifier\n", (name ? name + 1: argv[0]));
    printf("Install an IPA to an iOS device.\n");
}

int main(int argc, char **argv)
{
    char uuid[41];
    char* path = NULL;
    char* bundleId = NULL;
    if (argc == 3) {
        // Find the device automatically
        char **dev_list = NULL;
        int i;
        if (idevice_get_device_list(&dev_list, &i) < 0) {
            printf("ERROR: Unable to retrieve device list!\n");
            return -1;
        }
        if (i > 1) {
            printf("ERROR: More than 1 device found, please specify a UUID to install to, or unplug other devices");
            return -1;
        } else if (i == 1) {
            strcpy_s(uuid, _countof(uuid), dev_list[0]);
        } else {
            printf("ERROR: No device found");
            return -1;
        }
        idevice_device_list_free(dev_list);
        path = argv[1];
        bundleId = argv[2];
    } else if (argc == 4) {
        // Device id specified
        if (strlen(argv[1]) != 40) {
            print_usage(argc, argv);
            return 0;
        }
        strcpy_s(uuid, _countof(uuid), argv[1]);
        path = argv[2];
        bundleId = argv[3];
    } else {
        print_usage(argc, argv);
        return 0;
    }

    printf("Installing %s to: %s\n", path, uuid);

    // Phone pointer
    idevice_t phone = NULL;
    idevice_new(&phone, uuid);

    // Lockdown client
    lockdownd_client_t client = NULL;
    if (LOCKDOWN_E_SUCCESS != lockdownd_client_new_with_handshake(phone, &client, "win-ios-install")) {
        printf("ERROR: Connecting to device failed!\n");
        return -1;
    }

    // Start afc service
    lockdownd_service_descriptor_t service = NULL;
    if (LOCKDOWN_E_SUCCESS != lockdownd_start_service(client, "com.apple.afc", &service)) {
        printf("ERROR: Start installation service failed!\n");
        return -1;
    }

    // Create afc client
    afc_client_t afc_client = NULL;
    if (AFC_E_SUCCESS != afc_client_new(phone, service, &afc_client)) {
        printf("ERROR: Create afc client failed!\n");
        return -1;
    }

    // Open local file
    FILE *f;
    errno_t e;
    if ((e = fopen_s(&f, path, "rb")) != 0) {
        printf("ERROR: Failed to open ipa!\n");
        return -1;
    }

    // Make sure "PublicStaging" exists on the device
    char **strs;
    if (afc_get_file_info(afc_client, "PublicStaging", &strs) != AFC_E_SUCCESS) {
        if (afc_make_directory(afc_client, "PublicStaging") != AFC_E_SUCCESS) {
            fprintf(stderr, "WARNING: Could not create directory 'PublicStaging' on device!\n");
            return -1;
        }
    }
    
    // Copy file to AFC
    uint64_t af = NULL;
    if (AFC_E_SUCCESS != afc_file_open(afc_client, "PublicStaging/forge_app.ipa", AFC_FOPEN_WRONLY, &af)) {
        printf("ERROR: Failed to open remote file!\n");
        return -1;
    }

    size_t amount = 0;
    char buf[8192];
    do {
        amount = fread(buf, 1, sizeof(buf), f);
        if (amount > 0) {
            uint32_t written, total = 0;
            while (total < amount) {
                written = 0;
                if (afc_file_write(afc_client, af, buf, amount, &written) !=
                    AFC_E_SUCCESS) {
                    printf("ERROR: Failed to write remote file!\n");
                    return -1;
                    break;
                }
                total += written;
            }
            if (total != amount) {
                printf("ERROR: Failed to write remote file!\n");
                return -1;
            }
        }
    } while (amount > 0);

    afc_file_close(afc_client, af);
    fclose(f);

    // Start installation service
    service = NULL;
    if (LOCKDOWN_E_SUCCESS != lockdownd_start_service(client, "com.apple.mobile.installation_proxy", &service)) {
        printf("ERROR: Start installation service failed!\n");
        return -1;
    }

    // Create installation client
    instproxy_client_t install_client = NULL;
    if (INSTPROXY_E_SUCCESS != instproxy_client_new(phone, service, &install_client)) {
        printf("ERROR: Create installation client failed!\n");
        return -1;
    }

    // Installation options
    plist_t client_opts = instproxy_client_options_new();
    //instproxy_client_options_add(client_opts, "PackageType", "Developer");

    // Install app
    if (INSTPROXY_E_SUCCESS != instproxy_install(install_client, "PublicStaging/forge_app.ipa", client_opts, status_cb, NULL)) {
        printf("ERROR: Install app failed!\n");
        return -1;
    }
    
    // Wait until install complete
    while (op_completed == 0 && err_occured == 0) {
        Sleep(200);
    }

    if (err_occured) {
        return -1;
    }

    // Find app path
    char *app_path = NULL;

    client_opts = instproxy_client_options_new();
    instproxy_client_options_add(client_opts, "ApplicationType", "User", NULL);
    instproxy_error_t err;
    plist_t apps = NULL;

    err = instproxy_browse(install_client, client_opts, &apps);
    if (err != INSTPROXY_E_SUCCESS) {
        fprintf(stderr, "ERROR: instproxy_browse returned %d\n", err);
        return -1;
    }
    if (!apps || (plist_get_node_type(apps) != PLIST_ARRAY)) {
        fprintf(stderr,
                "ERROR: instproxy_browse returnd an invalid plist!\n");
        return -1;
    }

    uint32_t i = 0;
    for (i = 0; i < plist_array_get_size(apps); i++) {
        plist_t app = plist_array_get_item(apps, i);
        plist_t p_appid =
            plist_dict_get_item(app, "CFBundleIdentifier");
        char *s_appid = NULL;
        char *s_path = NULL;
        plist_t dispName =  plist_dict_get_item(app, "Path");

        if (p_appid) {
            plist_get_string_val(p_appid, &s_appid);
        }
        if (!s_appid) {
            fprintf(stderr, "ERROR: Failed to get APPID!\n");
            break;
        }

        if (dispName) {
            plist_get_string_val(dispName, &s_path);
        }

        if (!s_path) {
            s_path = _strdup(s_appid);
        }
        
        if (strcmp(bundleId, s_appid) == 0) {
            app_path = s_path;
        }
    }

    if (app_path == NULL) {
        printf("Bundle Id not found on device\n");
        return -1;
    }

    printf("Launching - %s\n", app_path);

    // Launch app
    if ((lockdownd_start_service(client, "com.apple.debugserver", &service) != LOCKDOWN_E_SUCCESS)) {
        printf("Could not start com.apple.debugserver!\n");
        return -1;
    }

    idevice_connection_t connection = NULL;
    if (idevice_connect(phone, service->port, &connection) != IDEVICE_E_SUCCESS) {
        fprintf(stderr, "idevice_connect failed!\n");
        return -1;
    }
    
    char* cmds[] = {
        "will be replaced with hex encoding of apppath",
        "Hc0",
        "c",
        NULL,
    };

    cmds[0] = (char*)malloc(2000);
    char* p = cmds[0];
    sprintf_s(p, 2000, "A%d,0,", strlen(app_path)*2);
    p += strlen(p);
    char* q = app_path;
    while (*q) {
        *p++ = tohex(*q >> 4);
        *p++ = tohex(*q & 0xf);
        q++;
    }

    char** cmd = cmds;
    while (*cmd) {
        Sleep(1000);
        send_pkt(*cmd, connection);
        recv_pkt(connection);
        recv_pkt(connection);
        cmd++;
    }

    while (1) {
        recv_pkt(connection);
        Sleep(100);
    }
}
