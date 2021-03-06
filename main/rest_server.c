#include <string.h>
#include <stdio.h>
#include <fcntl.h>
#include "esp_http_server.h"
#include "esp_system.h"
#include "esp_log.h"
#include "esp_err.h"
#include "esp_vfs.h"
#include "cJSON.h"

#include "freertos/FreeRTOS.h"
#include "freertos/event_groups.h"
#include "freertos/task.h"
#include "freertos/queue.h"
#include "driver/gpio.h"
#include "driver/ledc.h"

#include "PID.h"
#include "ds18b20.h"

PIDdata pid_defaults = {
    .target = 29.0f,

    .Kp = 53.4f,
    .Ki = 0.0f,
    .Kd = 0.0f,
    .dT = 100,

    .Outmin = 0,
    .Outmax = 1024,
    .Iterm_min = -54.0f,
    .Iterm_max = 54.0f
};
ptrPIDdata clima = &pid_defaults;

#define LEDC_TIMER              LEDC_TIMER_0
#define LEDC_MODE               LEDC_LOW_SPEED_MODE
#define LEDC_OUTPUT_IO          (13) // Define the output GPIO
#define LEDC_CHANNEL            LEDC_CHANNEL_0
#define LEDC_DUTY_RES           LEDC_TIMER_10_BIT // Set duty resolution to 13 bits
#define LEDC_DUTY               (1024) // Set duty to 100%.
#define LEDC_FREQUENCY          (1) // Frequency in Hertz. Set frequency at 5 kHz


#define DS_PIN 21 //GPIO where you connected ds18b20
DeviceAddress device_id = {40, 144, 20, 251, 12, 0, 0, 146};
TaskHandle_t sens_temp_Handle = NULL;
static void sens_temp_task(void* arg);
float temperature = 0;

TaskHandle_t controlHandle = NULL;
bool controller = false; // pri PID
static void control_task(void* arg);

QueueHandle_t queue;
typedef struct
{
  uint32_t timestamp;
  float measurement;
  float output;
  float Pterm;
  float Iterm;
  float Dterm;
} Data_t;

static const char *REST_TAG = "esp-rest";
#define REST_CHECK(a, str, goto_tag, ...)                                              \
    do                                                                                 \
    {                                                                                  \
        if (!(a))                                                                      \
        {                                                                              \
            ESP_LOGE(REST_TAG, "%s(%d): " str, __FUNCTION__, __LINE__, ##__VA_ARGS__); \
            goto goto_tag;                                                             \
        }                                                                              \
    } while (0)

#define FILE_PATH_MAX (ESP_VFS_PATH_MAX + 128)
#define SCRATCH_BUFSIZE (10240)

typedef struct rest_server_context {
    char base_path[ESP_VFS_PATH_MAX + 1];
    char scratch[SCRATCH_BUFSIZE];
} rest_server_context_t;

#define CHECK_FILE_EXTENSION(filename, ext) (strcasecmp(&filename[strlen(filename) - strlen(ext)], ext) == 0)

/* Set HTTP response content type according to file extension */
static esp_err_t set_content_type_from_file(httpd_req_t *req, const char *filepath)
{
    const char *type = "text/plain";
    if (CHECK_FILE_EXTENSION(filepath, ".html")) {
        type = "text/html";
    } else if (CHECK_FILE_EXTENSION(filepath, ".js")) {
        type = "application/javascript";
    } else if (CHECK_FILE_EXTENSION(filepath, ".css")) {
        type = "text/css";
    } else if (CHECK_FILE_EXTENSION(filepath, ".png")) {
        type = "image/png";
    } else if (CHECK_FILE_EXTENSION(filepath, ".ico")) {
        type = "image/x-icon";
    } else if (CHECK_FILE_EXTENSION(filepath, ".svg")) {
        type = "text/xml";
    }
    return httpd_resp_set_type(req, type);
}

/* Send HTTP response with the contents of the requested file */
static esp_err_t rest_common_get_handler(httpd_req_t *req)
{
    char filepath[FILE_PATH_MAX];

    rest_server_context_t *rest_context = (rest_server_context_t *)req->user_ctx;
    strlcpy(filepath, rest_context->base_path, sizeof(filepath));
    if (req->uri[strlen(req->uri) - 1] == '/') {
        strlcat(filepath, "/index.html", sizeof(filepath));
    } else {
        strlcat(filepath, req->uri, sizeof(filepath));
    }
    int fd = open(filepath, O_RDONLY, 0);
    if (fd == -1) {
        ESP_LOGE(REST_TAG, "Failed to open file : %s", filepath);
        /* Respond with 500 Internal Server Error */
        httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "Failed to read existing file");
        return ESP_FAIL;
    }

    set_content_type_from_file(req, filepath);

    char *chunk = rest_context->scratch;
    ssize_t read_bytes;
    do {
        /* Read file in chunks into the scratch buffer */
        read_bytes = read(fd, chunk, SCRATCH_BUFSIZE);
        if (read_bytes == -1) {
            ESP_LOGE(REST_TAG, "Failed to read file : %s", filepath);
        } else if (read_bytes > 0) {
            /* Send the buffer contents as HTTP response chunk */
            if (httpd_resp_send_chunk(req, chunk, read_bytes) != ESP_OK) {
                close(fd);
                ESP_LOGE(REST_TAG, "File sending failed!");
                /* Abort sending file */
                httpd_resp_sendstr_chunk(req, NULL);
                /* Respond with 500 Internal Server Error */
                httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "Failed to send file");
                return ESP_FAIL;
            }
        }
    } while (read_bytes > 0);
    /* Close file after sending complete */
    close(fd);
    ESP_LOGI(REST_TAG, "File sending complete");
    /* Respond with an empty chunk to signal HTTP response completion */
    httpd_resp_send_chunk(req, NULL, 0);
    return ESP_OK;
}

/* Simple handler for getting system handler */
static esp_err_t system_info_get_handler(httpd_req_t *req)
{
    httpd_resp_set_type(req, "application/json");
    cJSON *root = cJSON_CreateObject();
    esp_chip_info_t chip_info;
    esp_chip_info(&chip_info);
    cJSON_AddStringToObject(root, "version", IDF_VER);
    cJSON_AddNumberToObject(root, "cores", chip_info.cores);
    const char *sys_info = cJSON_Print(root);
    httpd_resp_sendstr(req, sys_info);
    free((void *)sys_info);
    cJSON_Delete(root);
    return ESP_OK;
}

// Clima handelers
static esp_err_t clima_init_get_handler(httpd_req_t *req)
{
    // Prepare and then apply the LEDC PWM timer configuration
    ledc_timer_config_t ledc_timer = {
        .speed_mode       = LEDC_MODE,
        .timer_num        = LEDC_TIMER,
        .duty_resolution  = LEDC_DUTY_RES,
        .freq_hz          = LEDC_FREQUENCY,
        .clk_cfg          = LEDC_AUTO_CLK
    };
    ESP_ERROR_CHECK(ledc_timer_config(&ledc_timer));

    // Prepare and then apply the LEDC PWM channel configuration
    ledc_channel_config_t ledc_channel = {
        .speed_mode     = LEDC_MODE,
        .channel        = LEDC_CHANNEL,
        .timer_sel      = LEDC_TIMER,
        .intr_type      = LEDC_INTR_DISABLE,
        .gpio_num       = LEDC_OUTPUT_IO,
        .duty           = LEDC_DUTY, // Set duty to 100%
        .hpoint         = 0
    };
    ESP_ERROR_CHECK(ledc_channel_config(&ledc_channel));


    queue = xQueueCreate(20, sizeof(Data_t));
    xTaskCreatePinnedToCore(control_task, "control_task", 4096, NULL, 1, &controlHandle,1); 
    if( controlHandle != NULL )
        vTaskSuspend(controlHandle); 
        
    xTaskCreatePinnedToCore(sens_temp_task, "sens_temp_task", 2048, NULL, 1, &sens_temp_Handle,1); 

    httpd_resp_set_type(req, "application/json");
    cJSON *root = cJSON_CreateObject();
    cJSON_AddNumberToObject(root, "Kp", clima->Kp);
    cJSON_AddNumberToObject(root, "Ki", clima->Ki);
    cJSON_AddNumberToObject(root, "Kd", clima->Kd);
    cJSON_AddNumberToObject(root, "dT", clima->dT);
    cJSON_AddNumberToObject(root, "target", clima->target);
    const char *sys_info = cJSON_Print(root);
    httpd_resp_sendstr(req, sys_info);
    free((void *)sys_info);
    cJSON_Delete(root);
    return ESP_OK;
}
static esp_err_t clima_update_parameters_post_handler(httpd_req_t *req)
{
    int total_len = req->content_len;
    int cur_len = 0;
    char *buf = ((rest_server_context_t *)(req->user_ctx))->scratch;
    int received = 0;
    if (total_len >= SCRATCH_BUFSIZE) {
       
        httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "content too long");
        return ESP_FAIL;
    }
    while (cur_len < total_len) {
        received = httpd_req_recv(req, buf + cur_len, total_len);
        if (received <= 0) {
          
            httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "Failed to post control value");
            return ESP_FAIL;
        }
        cur_len += received;
    }
    buf[total_len] = '\0';

    cJSON *root = cJSON_Parse(buf);
    PID_SetTuningParams(clima,cJSON_GetObjectItem(root, "Kp")->valuedouble,
                              cJSON_GetObjectItem(root, "Ki")->valuedouble,
                              cJSON_GetObjectItem(root, "Kd")->valuedouble); 
    PID_SetTraget(clima,cJSON_GetObjectItem(root, "target")->valuedouble,0);
    clima->dT =  cJSON_GetObjectItem(root, "dT")->valuedouble;
    ESP_LOGI(REST_TAG, "%f %f %f %f %f",clima->Kp,clima->Ki,clima->Kd,clima->target,clima->dT);
    cJSON_Delete(root);
    httpd_resp_sendstr(req, "Updated Prameters");
    return ESP_OK;
}
static esp_err_t clima_resume_get_handler(httpd_req_t *req)
{
    xQueueReset(queue);
    PID_ResetIterm(clima);
    if( controlHandle != NULL )
        vTaskResume(controlHandle);
    httpd_resp_sendstr(req, "Clima Resumed");
    return ESP_OK;
}
static esp_err_t clima_pause_get_handler(httpd_req_t *req)
{
    if( controlHandle != NULL )
        vTaskSuspend(controlHandle);
    ESP_ERROR_CHECK(ledc_set_duty(LEDC_MODE, LEDC_CHANNEL, 1024));
    ESP_ERROR_CHECK(ledc_update_duty(LEDC_MODE, LEDC_CHANNEL));
    httpd_resp_sendstr(req, "Clima Paused");
    return ESP_OK;
}
static esp_err_t clima_change_controller_get_handler(httpd_req_t *req)
{
    controller = !controller;
    httpd_resp_sendstr(req, "Controller Changed");
    return ESP_OK;
}
static esp_err_t clima_data_get_handler(httpd_req_t *req)
{
    httpd_resp_set_type(req, "application/json");
    cJSON *root = cJSON_CreateObject();
    char str[512];
    Data_t buf;
    int index = 0;  
    int num_el = uxQueueMessagesWaiting(queue);
    for (size_t j=0; j<num_el; j++){
        xQueueReceive(queue, &buf, 0);
        if (controller == false){ // pri PID
            index += snprintf(&str[index], 512-index, "%d %.2f %.2f %.2f %.2f %.2f ", 
            buf.timestamp,buf.Pterm, buf.Iterm, buf.Dterm, buf.output, buf.measurement);
        }else{
            index += snprintf(&str[index], 512-index, "%d %.2f %.2f ", 
            buf.timestamp, buf.output, buf.measurement);
        }
    }
    cJSON_AddStringToObject(root,"states",str);
    const char *sys_info = cJSON_Print(root);
    httpd_resp_sendstr(req, sys_info);
    free((void *)sys_info);
    cJSON_Delete(root);
    return ESP_OK;
}

// Clima tasks
static void control_task(void* arg)
{
    Data_t data;
    float output;
    while(1)
    {
        if (controller == true){//za bang bang
            if (temperature > clima->target){
                ESP_ERROR_CHECK(ledc_set_duty(LEDC_MODE, LEDC_CHANNEL, 1024));
                ESP_ERROR_CHECK(ledc_update_duty(LEDC_MODE, LEDC_CHANNEL));
                data.output = 0;
            }
            else{
                ESP_ERROR_CHECK(ledc_set_duty(LEDC_MODE, LEDC_CHANNEL, 0));
                ESP_ERROR_CHECK(ledc_update_duty(LEDC_MODE, LEDC_CHANNEL));
                data.output = 1;
            }
            data.timestamp = esp_log_timestamp();
            data.measurement = temperature;
        }else{
            output = PID_Update(clima,temperature);
            ESP_ERROR_CHECK(ledc_set_duty(LEDC_MODE, LEDC_CHANNEL, 1024 - (int) output));
            ESP_ERROR_CHECK(ledc_update_duty(LEDC_MODE, LEDC_CHANNEL));

            data.timestamp = esp_log_timestamp();
            data.measurement = temperature;
            data.output = output;
            data.Pterm =  clima->Pterm;
            data.Iterm =  clima->Iterm;
            data.Dterm =  clima->Dterm; 
        }
        if (xQueueSend(queue, &data, 0) != pdPASS){
            ESP_LOGI(REST_TAG, "meh %d %.2f %.2f %.2f %.2f %.2f", data.timestamp,data.Pterm, data.Iterm, data.Dterm, data.output, data.measurement);
            ESP_LOGI(REST_TAG, "meh %d",uxQueueMessagesWaiting(queue));
        }
        vTaskDelay(clima->dT / portTICK_PERIOD_MS);
    }
}
static void sens_temp_task(void* arg){
  ds18b20_init(DS_PIN);
  gpio_set_pull_mode(DS_PIN, GPIO_PULLUP_ONLY);
  ds18b20_setResolution((DeviceAddress *)device_id,1,11);
  while (1)
  {
    ds18b20_requestTemperatures();
    float temp = ds18b20_getTempC((DeviceAddress *)device_id);
    if (temp > -55.0) 
        temperature = temp;
    //ESP_LOGI(REST_TAG, "%fC",temperature);
    vTaskDelay(10 / portTICK_PERIOD_MS);
  }
}

esp_err_t start_rest_server(const char *base_path)
{
    REST_CHECK(base_path, "wrong base path", err);
    rest_server_context_t *rest_context = calloc(1, sizeof(rest_server_context_t));
    REST_CHECK(rest_context, "No memory for rest context", err);
    strlcpy(rest_context->base_path, base_path, sizeof(rest_context->base_path));

    httpd_handle_t server = NULL;
    httpd_config_t config = HTTPD_DEFAULT_CONFIG();
    config.uri_match_fn = httpd_uri_match_wildcard;

    ESP_LOGI(REST_TAG, "Starting HTTP Server");
    REST_CHECK(httpd_start(&server, &config) == ESP_OK, "Start server failed", err_start);

    /* URI handler for fetching system info */
    httpd_uri_t system_info_get_uri = {
        .uri = "/api/v1/system/info",
        .method = HTTP_GET,
        .handler = system_info_get_handler,
        .user_ctx = rest_context
    };
    httpd_register_uri_handler(server, &system_info_get_uri);

    // URI handelers for clima
    httpd_uri_t clima_init_get_uri = {
        .uri = "/api/v1/clima/init",
        .method = HTTP_GET,
        .handler = clima_init_get_handler,
        .user_ctx = rest_context
    };
    httpd_register_uri_handler(server, &clima_init_get_uri);

    httpd_uri_t clima_update_parameters_post_uri = {
        .uri = "/api/v1/clima/update_parameters",
        .method = HTTP_POST,
        .handler = clima_update_parameters_post_handler,
        .user_ctx = rest_context
    };
    httpd_register_uri_handler(server, &clima_update_parameters_post_uri);
    
    httpd_uri_t clima_resume_get_uri = {
        .uri = "/api/v1/clima/resume",
        .method = HTTP_GET,
        .handler = clima_resume_get_handler,
        .user_ctx = rest_context
    };
    httpd_register_uri_handler(server, &clima_resume_get_uri);

    httpd_uri_t clima_pause_get_uri = {
        .uri = "/api/v1/clima/pause",
        .method = HTTP_GET,
        .handler = clima_pause_get_handler,
        .user_ctx = rest_context
    };
    httpd_register_uri_handler(server, &clima_pause_get_uri);

    httpd_uri_t clima_change_controller_get_uri = {
        .uri = "/api/v1/clima/change_controller",
        .method = HTTP_GET,
        .handler = clima_change_controller_get_handler,
        .user_ctx = rest_context
    };
    httpd_register_uri_handler(server, &clima_change_controller_get_uri);

    /* URI handler for fetching clima data */
    httpd_uri_t clima_data_get_uri = {
        .uri = "/api/v1/clima/data",
        .method = HTTP_GET,
        .handler = clima_data_get_handler,
        .user_ctx = rest_context
    };
    httpd_register_uri_handler(server, &clima_data_get_uri);
    /* URI handler for getting web server files */
    httpd_uri_t common_get_uri = {
        .uri = "/*",
        .method = HTTP_GET,
        .handler = rest_common_get_handler,
        .user_ctx = rest_context
    };
    httpd_register_uri_handler(server, &common_get_uri);

    return ESP_OK;
err_start:
    free(rest_context);
err:
    return ESP_FAIL;
}
