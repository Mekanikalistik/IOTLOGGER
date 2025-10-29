#include <stdio.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_system.h"
#include "esp_log.h"
#include <flashdb.h>
#include <string.h>
#include "nvs_flash.h"
#include "esp_netif.h"
#include "esp_wifi.h"
#include "esp_event.h"
#include "esp_http_server.h"
#include "esp_vfs.h"
#include "esp_spiffs.h"
#include "driver/touch_sens.h"
#include "mdns.h"

static const char *TAG = "TOUCH_LOGGER";

// FlashDB TSDB for touch events
static struct fdb_tsdb tsdb = {0};

// Touch pad configuration
#define TOUCH_PAD_COUNT 7
static const int touch_pads[TOUCH_PAD_COUNT] = {
    1, 2, 3, 4, 5, 6, 7};
static const char *touch_pad_names[TOUCH_PAD_COUNT] = {
    "Touch_1", "Touch_2", "Touch_3", "Touch_4",
    "Touch_5", "Touch_6", "Touch_7"};

// Touch handles
static touch_sensor_handle_t touch_handle;
static touch_channel_handle_t channel_handles[TOUCH_PAD_COUNT];
uint32_t touch_thresholds[TOUCH_PAD_COUNT];

// HTTP server handle
static httpd_handle_t server = NULL;

static fdb_time_t get_time(void)
{
    // Return current time in milliseconds or seconds.
    // For simplicity, using FreeRTOS tick count here.
    // Consider using esp_timer_get_time() for microsecond resolution or an RTC.
    return xTaskGetTickCount() * portTICK_PERIOD_MS; // Time in milliseconds
}

static fdb_err_t tsdb_init(void)
{
    fdb_err_t result;
    ESP_LOGD(TAG, "Calling fdb_tsdb_init with name 'touch_events' and part_name 'flashdb'");
    result = fdb_tsdb_init(&tsdb, "touch_events", "flashdb", get_time, 128, NULL);
    if (result != FDB_NO_ERR)
    {
        ESP_LOGE(TAG, "fdb_tsdb_init failed with error code: %d", result);
    }
    else
    {
        ESP_LOGD(TAG, "fdb_tsdb_init returned FDB_NO_ERR");
    }
    return result;
}

// Touch sensor initialization
static void touch_sensor_init(void)
{
    // Initialize touch sensor peripheral
    touch_sensor_sample_config_t sample_cfg = {
        .charge_times = 500,
        .charge_volt_lim_l = TOUCH_VOLT_LIM_L_0V5,
        .charge_volt_lim_h = TOUCH_VOLT_LIM_H_2V2,
    };
    touch_sensor_config_t sens_cfg = TOUCH_SENSOR_DEFAULT_BASIC_CONFIG(1, &sample_cfg);
    ESP_ERROR_CHECK(touch_sensor_new_controller(&sens_cfg, &touch_handle));

    // Create and enable the new touch channel handles
    for (int i = 0; i < TOUCH_PAD_COUNT; i++)
    {
        touch_channel_config_t chan_cfg = {
            .active_thresh = {0}, // Will be updated after initial scanning
        };
        ESP_ERROR_CHECK(touch_sensor_new_channel(touch_handle, touch_pads[i], &chan_cfg, &channel_handles[i]));
        touch_chan_info_t chan_info = {};
        ESP_ERROR_CHECK(touch_sensor_get_channel_info(channel_handles[i], &chan_info));
        ESP_LOGI(TAG, "Touch [CH %d] enabled on GPIO%d", touch_pads[i], chan_info.chan_gpio);
    }

    // Configure the default filter for the touch sensor
    touch_sensor_filter_config_t filter_cfg = TOUCH_SENSOR_DEFAULT_FILTER_CONFIG();
    ESP_ERROR_CHECK(touch_sensor_config_filter(touch_handle, &filter_cfg));

    // Enable the touch sensor
    ESP_ERROR_CHECK(touch_sensor_enable(touch_handle));

    // Do initial scanning to initialize the touch channel data
    for (int i = 0; i < 3; i++)
    {
        ESP_ERROR_CHECK(touch_sensor_trigger_oneshot_scanning(touch_handle, 2000));
    }

    // Read initial channel benchmark and set thresholds
    for (int i = 0; i < TOUCH_PAD_COUNT; i++)
    {
        uint32_t benchmark;
        ESP_ERROR_CHECK(touch_channel_read_data(channel_handles[i], TOUCH_CHAN_DATA_TYPE_BENCHMARK, &benchmark));
        touch_thresholds[i] = benchmark * 2 / 3; // Set threshold to 2/3 of benchmark value
        ESP_LOGI(TAG, "Touch pad %d threshold: %" PRIu32 " (benchmark: %" PRIu32 ")", i + 1, touch_thresholds[i], benchmark);
    }

    // Start continuous scanning
    ESP_ERROR_CHECK(touch_sensor_start_continuous_scanning(touch_handle));
}

// Touch detection task
static void touch_detection_task(void *pvParameter)
{
    while (1)
    {
        for (int i = 0; i < TOUCH_PAD_COUNT; i++)
        {
            uint32_t touch_value;
            ESP_ERROR_CHECK(touch_channel_read_data(channel_handles[i], TOUCH_CHAN_DATA_TYPE_SMOOTH, &touch_value));

            if (touch_value < touch_thresholds[i])
            {
                // Touch detected
                struct tm timeinfo;
                time_t now;
                time(&now);
                localtime_r(&now, &timeinfo);

                char timestamp[32];
                strftime(timestamp, sizeof(timestamp), "%Y-%m-%d %H:%M:%S", &timeinfo);

                char touch_data[128];
                snprintf(touch_data, sizeof(touch_data), "{\"pad\":\"%s\",\"user\":\"User_%d\",\"timestamp\":\"%s\"}",
                         touch_pad_names[i], (i % 3) + 1, timestamp);

                struct fdb_blob blob;
                fdb_tsl_append(&tsdb, fdb_blob_make(&blob, touch_data, strlen(touch_data)));

                ESP_LOGI(TAG, "Touch detected on %s by User_%d at %s", touch_pad_names[i], (i % 3) + 1, timestamp);

                // Debounce delay
                vTaskDelay(pdMS_TO_TICKS(500));
            }
        }
        vTaskDelay(pdMS_TO_TICKS(50));
    }
}

// HTTP handlers
static esp_err_t root_get_handler(httpd_req_t *req)
{
    FILE *f = fopen("/spiffs/index.html", "r");
    if (f == NULL)
    {
        httpd_resp_send_404(req);
        return ESP_FAIL;
    }
    char buf[1024];
    size_t read;
    httpd_resp_set_type(req, "text/html");
    while ((read = fread(buf, 1, sizeof(buf), f)) > 0)
    {
        httpd_resp_send_chunk(req, buf, read);
    }
    httpd_resp_send_chunk(req, NULL, 0);
    fclose(f);
    return ESP_OK;
}

static esp_err_t css_get_handler(httpd_req_t *req)
{
    FILE *f = fopen("/spiffs/style.css", "r");
    if (f == NULL)
    {
        httpd_resp_send_404(req);
        return ESP_FAIL;
    }
    char buf[1024];
    size_t read;
    httpd_resp_set_type(req, "text/css");
    while ((read = fread(buf, 1, sizeof(buf), f)) > 0)
    {
        httpd_resp_send_chunk(req, buf, read);
    }
    httpd_resp_send_chunk(req, NULL, 0);
    fclose(f);
    return ESP_OK;
}

static esp_err_t js_get_handler(httpd_req_t *req)
{
    FILE *f = fopen("/spiffs/script.js", "r");
    if (f == NULL)
    {
        httpd_resp_send_404(req);
        return ESP_FAIL;
    }
    char buf[1024];
    size_t read;
    httpd_resp_set_type(req, "application/javascript");
    while ((read = fread(buf, 1, sizeof(buf), f)) > 0)
    {
        httpd_resp_send_chunk(req, buf, read);
    }
    httpd_resp_send_chunk(req, NULL, 0);
    fclose(f);
    return ESP_OK;
}

static esp_err_t api_touch_logs_handler(httpd_req_t *req)
{
    // Simplified response without cJSON for now
    const char *sample_response = "[{\"timestamp\":\"2024-01-01 12:00:00\",\"pad\":\"Touch_1\",\"user\":\"User_1\"}]";

    httpd_resp_set_type(req, "application/json");
    httpd_resp_send(req, sample_response, strlen(sample_response));
    return ESP_OK;
}

static esp_err_t api_csv_export_handler(httpd_req_t *req)
{
    httpd_resp_set_type(req, "text/csv");
    httpd_resp_set_hdr(req, "Content-Disposition", "attachment; filename=touch_logs.csv");

    // CSV header
    httpd_resp_send_chunk(req, "Timestamp,Touch_Pad,User\n", 27);

    // Sample CSV data (in real implementation, query actual TSDB data)
    const char *sample_data[] = {
        "2024-01-01 12:00:00,Touch_1,User_1\n",
        "2024-01-01 12:05:00,Touch_2,User_2\n",
        "2024-01-01 12:10:00,Touch_3,User_3\n"};

    for (int i = 0; i < 3; i++)
    {
        httpd_resp_send_chunk(req, (char *)sample_data[i], strlen(sample_data[i]));
    }

    httpd_resp_send_chunk(req, NULL, 0);
    return ESP_OK;
}

void app_main()
{
    ESP_LOGI(TAG, "Starting Touch Sensor Logger with FlashDB");

    // Initialize NVS
    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND)
    {
        ESP_ERROR_CHECK(nvs_flash_erase());
        ret = nvs_flash_init();
    }
    ESP_ERROR_CHECK(ret);

    // Initialize TCP/IP stack
    ESP_ERROR_CHECK(esp_netif_init());

    // Create default event loop
    ESP_ERROR_CHECK(esp_event_loop_create_default());

    // Create default WiFi AP
    esp_netif_create_default_wifi_ap();

    // WiFi configuration
    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&cfg));

    wifi_config_t wifi_config = {
        .ap = {
            .ssid = "ratsf-proto",
            .ssid_len = strlen("ratsf-proto"),
            .channel = 1,
            .password = "61136113",
            .max_connection = 4,
            .authmode = WIFI_AUTH_WPA2_PSK},
    };

    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_AP));
    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_AP, &wifi_config));
    ESP_ERROR_CHECK(esp_wifi_start());

    // Initialize mDNS
    ESP_ERROR_CHECK(mdns_init());
    ESP_ERROR_CHECK(mdns_hostname_set("ratsf-proto"));
    ESP_ERROR_CHECK(mdns_instance_name_set("ESP32 Touch Logger"));

    ESP_LOGI(TAG, "WiFi AP started. Connect to SSID: %s, Password: %s", wifi_config.ap.ssid, wifi_config.ap.password);
    ESP_LOGI(TAG, "Web UI available at: http://ratsf-proto.local or http://192.168.4.1");

    // Initialize SPIFFS
    esp_vfs_spiffs_conf_t conf = {
        .base_path = "/spiffs",
        .partition_label = NULL,
        .max_files = 5,
        .format_if_mount_failed = true};
    ret = esp_vfs_spiffs_register(&conf);
    if (ret != ESP_OK)
    {
        ESP_LOGE(TAG, "Failed to mount SPIFFS (%s)", esp_err_to_name(ret));
        return;
    }

    size_t total = 0, used = 0;
    ret = esp_spiffs_info(NULL, &total, &used);
    if (ret != ESP_OK)
    {
        ESP_LOGE(TAG, "Failed to get SPIFFS partition information (%s)", esp_err_to_name(ret));
    }
    else
    {
        ESP_LOGI(TAG, "SPIFFS mounted. Total: %d KB, Used: %d KB", total / 1024, used / 1024);
    }

    // Erase FlashDB partition before initialization
    const esp_partition_t *flashdb_part = esp_partition_find_first(0x40, 0x00, "flashdb");
    if (flashdb_part)
    {
        ESP_ERROR_CHECK(esp_partition_erase_range(flashdb_part, 0, flashdb_part->size));
        ESP_LOGI(TAG, "FlashDB partition erased");
    }
    else
    {
        ESP_LOGE(TAG, "FlashDB partition not found!");
        return;
    }

    // Initialize FlashDB TSDB
    fdb_err_t result = tsdb_init();
    if (result != FDB_NO_ERR)
    {
        ESP_LOGE(TAG, "TSDB init failed: %d", result);
        return;
    }
    ESP_LOGI(TAG, "FlashDB TSDB initialized successfully");

    // Initialize touch sensors
    touch_sensor_init();
    ESP_LOGI(TAG, "Touch sensors initialized on GPIO 4-10");

    // Start HTTP server
    httpd_config_t config = HTTPD_DEFAULT_CONFIG();
    config.server_port = 80;

    if (httpd_start(&server, &config) == ESP_OK)
    {
        // Register URI handlers
        httpd_uri_t root = {
            .uri = "/",
            .method = HTTP_GET,
            .handler = root_get_handler,
            .user_ctx = NULL};
        httpd_register_uri_handler(server, &root);

        httpd_uri_t css = {
            .uri = "/style.css",
            .method = HTTP_GET,
            .handler = css_get_handler,
            .user_ctx = NULL};
        httpd_register_uri_handler(server, &css);

        httpd_uri_t js = {
            .uri = "/script.js",
            .method = HTTP_GET,
            .handler = js_get_handler,
            .user_ctx = NULL};
        httpd_register_uri_handler(server, &js);

        httpd_uri_t api_logs = {
            .uri = "/api/touch-logs",
            .method = HTTP_GET,
            .handler = api_touch_logs_handler,
            .user_ctx = NULL};
        httpd_register_uri_handler(server, &api_logs);

        httpd_uri_t api_csv = {
            .uri = "/api/export-csv",
            .method = HTTP_GET,
            .handler = api_csv_export_handler,
            .user_ctx = NULL};
        httpd_register_uri_handler(server, &api_csv);

        ESP_LOGI(TAG, "HTTP server started on port 80");
    }
    else
    {
        ESP_LOGE(TAG, "Failed to start HTTP server");
    }

    // Start touch detection task
    xTaskCreate(touch_detection_task, "touch_task", 2048, NULL, 5, NULL);

    ESP_LOGI(TAG, "Touch Sensor Logger is running. Touch sensors 1-7 (GPIO 4-10) to log events.");

    // Keep running
    while (1)
    {
        vTaskDelay(pdMS_TO_TICKS(60000)); // Log status every minute
        ESP_LOGI(TAG, "System running...");
    }
}