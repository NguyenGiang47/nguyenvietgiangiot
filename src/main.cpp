#include <Arduino.h>
#include <WiFi.h>
#include <HTTPClient.h>
#include <driver/i2s.h>
#include <esp_heap_caps.h>
#include <math.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>

#include "model_data_3classes_5s.h"

#include "tensorflow/lite/micro/micro_interpreter.h"
#include "tensorflow/lite/micro/micro_mutable_op_resolver.h"
#include "tensorflow/lite/micro/micro_error_reporter.h"
#include "tensorflow/lite/micro/system_setup.h"
#include "tensorflow/lite/schema/schema_generated.h"
// ============================================================
// ESP32-S3 + INMP441
// ============================================================

#define I2S_PORT I2S_NUM_0

#define I2S_BCLK 12
#define I2S_LRCL 13
#define I2S_DIN 14

// ============================================================
// AUDIO
// ============================================================

constexpr int SAMPLE_RATE = 16000;

// Model hien tai duoc train voi cua so 5 giay.
// KHONG doi MODEL_SECONDS neu van dung model_data_3classes_5s.h.
constexpr int MODEL_SECONDS = 5;

// Bo dem lich su 10 giay de gui WAV len web.
constexpr int HISTORY_SECONDS = 10;

// AI chay lai moi 1 giay tren 5 giay am thanh moi nhat.
constexpr int INFERENCE_INTERVAL_MS = 1000;

constexpr int MODEL_SAMPLES = SAMPLE_RATE * MODEL_SECONDS;     // 80000
constexpr int HISTORY_SAMPLES = SAMPLE_RATE * HISTORY_SECONDS; // 160000
constexpr int LEVEL_SAMPLES = SAMPLE_RATE;                     // 1 giay

// Chỉ bỏ qua đoạn gần như im lặng. Để -60 dBFS giúp không bỏ sót
// tiếng khóc nhỏ hoặc tiếng đập ở xa; model vẫn quyết định nhãn cuối.
constexpr float SOUND_TRIGGER_DBFS = -60.0f;

// Tranh gui cung mot canh bao len web lien tuc moi giay.
constexpr uint32_t EVENT_COOLDOWN_MS = 10000;

// Khi vừa đủ 5 giây dữ liệu, chờ thêm một chút để tín hiệu ổn định.
// Điều này tránh lần suy luận đầu bị nhảy do DMA/micro vừa khởi động.
constexpr uint32_t STARTUP_STABILIZE_MS = 3000;

// Cần thấy cùng một nhãn ở 2 lần suy luận liên tiếp mới tạo cảnh báo.
// AI vẫn chạy mỗi giây nên thời gian xác nhận tối đa khoảng 2 giây.
constexpr uint8_t ALERT_CONFIRMATIONS_REQUIRED = 2;

// Mất Wi-Fi/server tạm thời không được làm mất cảnh báo.
constexpr uint8_t UPLOAD_ATTEMPTS = 3;
constexpr uint32_t UPLOAD_RETRY_DELAY_MS = 1500;

// May tinh chay web_server.py va ESP32 phai cung ket noi router nay.
const char *WEB_SERVER_URL = "http://192.168.0.103:5000";
const char *DEVICE_ID = "ESP32S3_01";

// Thay WIFI_PASSWORD bang mat khau cua TP-Link_9152 truoc khi nap code.
const char *WIFI_SSID = "HoaHong";
const char *WIFI_PASSWORD = "thanhhien";

uint32_t eventSequence = 0;
uint32_t lastKhocUploadMs = 0;
uint32_t lastDapPhaUploadMs = 0;
bool hasKhocUpload = false;
bool hasDapPhaUpload = false;

// Giữ lại cảnh báo và WAV khi Wi-Fi/server tạm thời lỗi.
bool pendingUpload = false;
char pendingLabel[12] = {};
float pendingConfidence = 0.0f;
float pendingKhoc = 0.0f;
float pendingDapPha = 0.0f;
String pendingEventId;

const char *wifiStatusText(wl_status_t status)
{
    switch (status)
    {
    case WL_NO_SSID_AVAIL:
        return "khong thay SSID";
    case WL_CONNECT_FAILED:
        return "sai mat khau/ket noi that bai";
    case WL_CONNECTION_LOST:
        return "mat ket noi";
    case WL_DISCONNECTED:
        return "da ngat ket noi";
    case WL_CONNECTED:
        return "da ket noi";
    default:
        return "trang thai khac";
    }
}

// ============================================================
// LOG-MEL
// Model của bạn:
// input = [1, 40, 501, 1]
// ============================================================

constexpr int N_FFT = 512;
constexpr int FFT_BINS = N_FFT / 2 + 1; // 257

constexpr int HOP_LENGTH = 160;

constexpr int N_MELS = 40;
constexpr int N_FRAMES = 501;

constexpr float TOP_DB = 80.0f;

constexpr float PI_F = 3.14159265358979323846f;

// ============================================================
// CLASS INDEX
//
// QUAN TRỌNG:
// Phần .tflite không chứa tên class.
//
// Tôi đang đặt thứ tự:
//   0 = KHÓC
//   1 = ĐẬP PHÁ
//   2 = OTHER
//
// Nó PHẢI giống thứ tự lúc train.
// ============================================================

constexpr int IDX_KHOC = 0;
constexpr int IDX_DAP_PHA = 1;
constexpr int IDX_OTHER = 2;

// Ngưỡng cảnh báo theo yêu cầu:
// - KHOC: chỉ cần >= 20% là cảnh báo ngay.
// - DAP_PHA: giữ ngưỡng cao hơn để giảm báo giả.
constexpr float KHOC_ALERT_THRESHOLD = 0.20f;
constexpr float DAP_PHA_ALERT_THRESHOLD = 0.70f;

// Với ĐẬP PHÁ vẫn yêu cầu class cao nhất hơn class thứ 2.
constexpr float MIN_MARGIN = 0.10f;

// Nếu micro gần như im lặng
constexpr float SILENCE_DBFS = -60.0f;

// ============================================================
// TENSORFLOW LITE MICRO
// ============================================================

const tflite::Model *model = nullptr;
tflite::MicroInterpreter *interpreter = nullptr;

TfLiteTensor *inputTensor = nullptr;
TfLiteTensor *outputTensor = nullptr;

// Model có tensor trung gian khá lớn.
// ESP32-S3 nên có PSRAM.
constexpr size_t TENSOR_ARENA_SIZE = 2 * 1024 * 1024;

uint8_t *tensorArenaRaw = nullptr;
uint8_t *tensorArena = nullptr;

// ============================================================
// BUFFERS
// ============================================================

// Ring buffer 10 giay: micro ghi lien tuc vao day.
int16_t *audioRingBuffer = nullptr;

// Snapshot 5 giay moi nhat de dua vao model.
int16_t *modelAudioBuffer = nullptr;

// Snapshot 10 giay moi nhat de gui WAV len web khi co canh bao.
int16_t *uploadAudioBuffer = nullptr;

// Vi tri ghi hien tai cua ring buffer va tong so sample da thu.
volatile size_t audioWritePos = 0;
volatile uint64_t totalSamplesCaptured = 0;
portMUX_TYPE audioMux = portMUX_INITIALIZER_UNLOCKED;
TaskHandle_t audioCaptureTaskHandle = nullptr;

// 40 x 501 log-mel
float *logMelBuffer = nullptr;

// 40 x 257 mel filter weights
float *melWeights = nullptr;

// FFT
float fftReal[N_FFT];
float fftImag[N_FFT];
float fftPower[FFT_BINS];

float hannWindow[N_FFT];

// ============================================================
// PSRAM ALLOCATION
// ============================================================

void *allocPSRAM(size_t size)
{
    return heap_caps_malloc(
        size,
        MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
}

// ============================================================
// I2S / INMP441
// ============================================================

bool initMicrophone()
{
    i2s_config_t config = {};

    config.mode =
        (i2s_mode_t)(I2S_MODE_MASTER |
                     I2S_MODE_RX);

    config.sample_rate = SAMPLE_RATE;

    config.bits_per_sample =
        I2S_BITS_PER_SAMPLE_32BIT;

    // Đọc cả hai khe I2S để dùng được dù chân L/R của INMP441 nối GND hay 3V3.
    config.channel_format =
        I2S_CHANNEL_FMT_RIGHT_LEFT;

    config.communication_format =
        I2S_COMM_FORMAT_STAND_I2S;

    config.intr_alloc_flags =
        ESP_INTR_FLAG_LEVEL1;

    config.dma_buf_count = 8;
    config.dma_buf_len = 512;

    config.use_apll = false;
    config.tx_desc_auto_clear = false;
    config.fixed_mclk = 0;

    i2s_pin_config_t pins = {};

    pins.mck_io_num = I2S_PIN_NO_CHANGE;
    pins.bck_io_num = I2S_BCLK;
    pins.ws_io_num = I2S_LRCL;
    pins.data_out_num = I2S_PIN_NO_CHANGE;
    pins.data_in_num = I2S_DIN;

    esp_err_t err;

    err = i2s_driver_install(
        I2S_PORT,
        &config,
        0,
        nullptr);

    if (err != ESP_OK)
    {
        Serial.printf(
            "Loi i2s_driver_install: %d\n",
            err);

        return false;
    }

    err = i2s_set_pin(
        I2S_PORT,
        &pins);

    if (err != ESP_OK)
    {
        Serial.printf(
            "Loi i2s_set_pin: %d\n",
            err);

        return false;
    }

    i2s_zero_dma_buffer(I2S_PORT);

    Serial.println("INMP441: OK");

    return true;
}

// ============================================================
// THU AM LIEN TUC VAO RING BUFFER 10 GIAY
// ============================================================

void audioCaptureTask(void *parameter)
{
    static int32_t i2sBuffer[1024];
    static int16_t pcmBuffer[512];

    Serial.println("[Audio] Task thu am lien tuc da bat.");

    while (true)
    {
        size_t bytesRead = 0;

        esp_err_t err = i2s_read(
            I2S_PORT,
            i2sBuffer,
            sizeof(i2sBuffer),
            &bytesRead,
            portMAX_DELAY);

        if (err != ESP_OK)
        {
            Serial.printf("[Audio] Loi i2s_read: %d", err);
            vTaskDelay(pdMS_TO_TICKS(20));
            continue;
        }

        // Khi đọc stereo, mỗi sample gồm một word trái và một word phải.
        const int frameCount = bytesRead / (2 * sizeof(int32_t));

        for (int i = 0; i < frameCount; ++i)
        {
            // INMP441 chỉ phát tín hiệu ở một trong hai khe tùy chân L/R.
            int32_t left = i2sBuffer[2 * i];
            int32_t right = i2sBuffer[2 * i + 1];
            int32_t sample = abs(left) >= abs(right) ? left : right;

            // Lấy 16 bit có ý nghĩa từ word I2S 32-bit.
            sample >>= 15;

            if (sample > 32767)
                sample = 32767;
            if (sample < -32768)
                sample = -32768;

            pcmBuffer[i] = (int16_t)sample;
        }

        // Chi khoa ring buffer trong luc copy nhanh mot block.
        portENTER_CRITICAL(&audioMux);

        for (int i = 0; i < frameCount; ++i)
        {
            audioRingBuffer[audioWritePos] = pcmBuffer[i];

            audioWritePos++;
            if (audioWritePos >= HISTORY_SAMPLES)
                audioWritePos = 0;
        }

        totalSamplesCaptured += frameCount;

        portEXIT_CRITICAL(&audioMux);
    }
}

// Lay N sample moi nhat tu ring buffer thanh mot mang lien tuc.
// Neu he thong moi khoi dong chua du N sample, phan dau se duoc zero-pad.
void snapshotLatestAudio(int16_t *dest, size_t sampleCount)
{
    if (!dest || sampleCount == 0)
        return;
    if (sampleCount > HISTORY_SAMPLES)
        sampleCount = HISTORY_SAMPLES;

    size_t writePosSnapshot = 0;
    uint64_t capturedSnapshot = 0;

    // Chi khoa rat ngan de lay trang thai ring buffer.
    portENTER_CRITICAL(&audioMux);
    writePosSnapshot = audioWritePos;
    capturedSnapshot = totalSamplesCaptured;
    portEXIT_CRITICAL(&audioMux);

    size_t available = (capturedSnapshot < (uint64_t)HISTORY_SAMPLES)
                           ? (size_t)capturedSnapshot
                           : (size_t)HISTORY_SAMPLES;
    size_t copyCount = (available < sampleCount) ? available : sampleCount;
    size_t zeroCount = sampleCount - copyCount;

    if (zeroCount > 0)
        memset(dest, 0, zeroCount * sizeof(int16_t));

    size_t start = (writePosSnapshot + HISTORY_SAMPLES - copyCount) % HISTORY_SAMPLES;
    size_t firstPart = min(copyCount, HISTORY_SAMPLES - start);

    if (firstPart > 0)
        memcpy(dest + zeroCount, audioRingBuffer + start, firstPart * sizeof(int16_t));

    size_t secondPart = copyCount - firstPart;
    if (secondPart > 0)
        memcpy(dest + zeroCount + firstPart, audioRingBuffer, secondPart * sizeof(int16_t));
}

float calculateRMSdBFS(
    const int16_t *samples,
    size_t sampleCount)
{
    if (!samples || sampleCount == 0)
        return -120.0f;

    double sum = 0.0;

    for (size_t i = 0; i < sampleCount; ++i)
    {
        float x =
            (float)samples[i] /
            32768.0f;

        sum += x * x;
    }

    float rms =
        sqrtf(
            (float)(sum / sampleCount));

    if (rms < 0.000001f)
        rms = 0.000001f;

    return 20.0f * log10f(rms);
}

// ============================================================
// FFT RADIX-2
// Không cần thư viện FFT ngoài
// ============================================================

void fft512(float *real, float *imag)
{
    // Bit reversal
    int j = 0;

    for (int i = 1; i < N_FFT; i++)
    {
        int bit = N_FFT >> 1;

        while (j & bit)
        {
            j ^= bit;
            bit >>= 1;
        }

        j ^= bit;

        if (i < j)
        {
            float tmp;

            tmp = real[i];
            real[i] = real[j];
            real[j] = tmp;

            tmp = imag[i];
            imag[i] = imag[j];
            imag[j] = tmp;
        }
    }

    // Cooley-Tukey
    for (
        int len = 2;
        len <= N_FFT;
        len <<= 1)
    {
        float angle =
            -2.0f * PI_F / len;

        float wLenReal =
            cosf(angle);

        float wLenImag =
            sinf(angle);

        for (
            int i = 0;
            i < N_FFT;
            i += len)
        {
            float wReal = 1.0f;
            float wImag = 0.0f;

            int half = len >> 1;

            for (int k = 0; k < half; k++)
            {
                int even = i + k;
                int odd = even + half;

                float oddReal =
                    real[odd] * wReal -
                    imag[odd] * wImag;

                float oddImag =
                    real[odd] * wImag +
                    imag[odd] * wReal;

                float evenReal =
                    real[even];

                float evenImag =
                    imag[even];

                real[even] =
                    evenReal + oddReal;

                imag[even] =
                    evenImag + oddImag;

                real[odd] =
                    evenReal - oddReal;

                imag[odd] =
                    evenImag - oddImag;

                float nextWReal =
                    wReal * wLenReal -
                    wImag * wLenImag;

                float nextWImag =
                    wReal * wLenImag +
                    wImag * wLenReal;

                wReal = nextWReal;
                wImag = nextWImag;
            }
        }
    }
}

// ============================================================
// LIBROSA-SLANEY MEL SCALE
// ============================================================

float hzToMel(float hz)
{
    constexpr float F_SP = 200.0f / 3.0f;
    constexpr float MIN_LOG_HZ = 1000.0f;

    float mel;

    if (hz < MIN_LOG_HZ)
    {
        mel = hz / F_SP;
    }
    else
    {
        constexpr float MIN_LOG_MEL =
            MIN_LOG_HZ / F_SP;

        constexpr float LOG_STEP =
            0.068751777f; // log(6.4) / 27

        mel =
            MIN_LOG_MEL +
            logf(hz / MIN_LOG_HZ) /
                LOG_STEP;
    }

    return mel;
}

float melToHz(float mel)
{
    constexpr float F_SP = 200.0f / 3.0f;
    constexpr float MIN_LOG_HZ = 1000.0f;
    constexpr float MIN_LOG_MEL =
        MIN_LOG_HZ / F_SP;

    constexpr float LOG_STEP =
        0.068751777f;

    if (mel < MIN_LOG_MEL)
    {
        return mel * F_SP;
    }

    return MIN_LOG_HZ *
           expf(
               LOG_STEP *
               (mel - MIN_LOG_MEL));
}

// ============================================================
// TẠO HANN WINDOW
// ============================================================

void createHannWindow()
{
    for (int i = 0; i < N_FFT; i++)
    {
        hannWindow[i] =
            0.5f -
            0.5f *
                cosf(
                    2.0f *
                    PI_F *
                    i /
                    N_FFT);
    }
}

// ============================================================
// TẠO MEL FILTER BANK
// 40 MEL x 257 FFT bins
// ============================================================

void createMelFilterBank()
{
    float melPoints[N_MELS + 2];
    float hzPoints[N_MELS + 2];

    float minMel =
        hzToMel(0.0f);

    float maxMel =
        hzToMel(
            SAMPLE_RATE / 2.0f);

    for (
        int i = 0;
        i < N_MELS + 2;
        i++)
    {
        float mel =
            minMel +
            (maxMel - minMel) *
                i /
                (N_MELS + 1);

        melPoints[i] = mel;
        hzPoints[i] =
            melToHz(mel);
    }

    for (int m = 0; m < N_MELS; m++)
    {
        float left =
            hzPoints[m];

        float center =
            hzPoints[m + 1];

        float right =
            hzPoints[m + 2];

        /*
         * Slaney normalization.
         */
        float enorm =
            2.0f /
            (right - left);

        for (
            int k = 0;
            k < FFT_BINS;
            k++)
        {
            float frequency =
                (float)k *
                SAMPLE_RATE /
                N_FFT;

            float weight = 0.0f;

            if (
                frequency >= left &&
                frequency <= center)
            {
                weight =
                    (frequency - left) /
                    (center - left);
            }
            else if (
                frequency > center &&
                frequency <= right)
            {
                weight =
                    (right - frequency) /
                    (right - center);
            }

            weight *= enorm;

            melWeights[m * FFT_BINS + k] = weight;
        }
    }

    Serial.println(
        "Mel filter bank: OK");
}

// ============================================================
// AUDIO -> LOG-MEL
//
// Output:
// 40 x 501
// ============================================================

bool calculateLogMel()
{
    Serial.println(
        "Dang tinh Log-Mel...");

    float maxLogEnergy =
        -1000000.0f;

    // --------------------------------------------------------
    // center=True:
    //
    // frame 0 bắt đầu ở -256
    // frame cuối có zero padding.
    //
    // 5s / hop=160 -> 501 frames
    // --------------------------------------------------------

    for (
        int frame = 0;
        frame < N_FRAMES;
        frame++)
    {
        int audioStart =
            frame * HOP_LENGTH -
            N_FFT / 2;

        // ----------------------------------------------------
        // Window
        // ----------------------------------------------------

        for (int n = 0; n < N_FFT; n++)
        {
            int audioIndex =
                audioStart + n;

            float sample = 0.0f;

            if (
                audioIndex >= 0 &&
                audioIndex < MODEL_SAMPLES)
            {
                sample =
                    (float)modelAudioBuffer[audioIndex] /
                    32768.0f;
            }

            fftReal[n] =
                sample *
                hannWindow[n];

            fftImag[n] = 0.0f;
        }

        // ----------------------------------------------------
        // FFT
        // ----------------------------------------------------

        fft512(
            fftReal,
            fftImag);

        // Tinh cong suat FFT mot lan cho moi frame, tranh tinh lai 40 lan.
        for (int k = 0; k < FFT_BINS; k++)
        {
            fftPower[k] =
                fftReal[k] * fftReal[k] +
                fftImag[k] * fftImag[k];
        }

        // ----------------------------------------------------
        // Mỗi Mel bin
        // ----------------------------------------------------

        for (
            int mel = 0;
            mel < N_MELS;
            mel++)
        {
            float energy = 0.0f;

            for (
                int k = 0;
                k < FFT_BINS;
                k++)
            {
                float weight =
                    melWeights[mel *
                                   FFT_BINS +
                               k];

                energy +=
                    fftPower[k] *
                    weight;
            }

            if (energy < 1e-10f)
            {
                energy = 1e-10f;
            }

            // Power -> dB
            float logEnergy =
                10.0f *
                log10f(energy);

            logMelBuffer[mel *
                             N_FRAMES +
                         frame] = logEnergy;

            if (
                logEnergy >
                maxLogEnergy)
            {
                maxLogEnergy =
                    logEnergy;
            }
        }

        if ((frame % 100) == 0)
        {
            Serial.printf(
                "LogMel frame %d/%d\n",
                frame,
                N_FRAMES);
        }
    }

    // --------------------------------------------------------
    // ref=max, top_db=80
    // sau đó normalize -> [0,1]
    // --------------------------------------------------------

    const int featureCount =
        N_MELS *
        N_FRAMES;

    for (
        int i = 0;
        i < featureCount;
        i++)
    {
        float relativeDB =
            logMelBuffer[i] -
            maxLogEnergy;

        if (relativeDB < -TOP_DB)
        {
            relativeDB =
                -TOP_DB;
        }

        /*
         * -80 dB -> 0
         *   0 dB -> 1
         */
        float normalized =
            (relativeDB + TOP_DB) /
            TOP_DB;

        if (normalized < 0.0f)
            normalized = 0.0f;

        if (normalized > 1.0f)
            normalized = 1.0f;

        logMelBuffer[i] =
            normalized;
    }

    Serial.println(
        "Log-Mel: OK");

    return true;
}

// ============================================================
// INIT TFLITE
// ============================================================

bool initModel()
{
    tflite::InitializeTarget();

    model =
        tflite::GetModel(
            g_audio_model_data_5s);

    if (
        model->version() !=
        TFLITE_SCHEMA_VERSION)
    {
        Serial.printf(
            "Schema sai! Model=%d Runtime=%d\n",
            model->version(),
            TFLITE_SCHEMA_VERSION);

        return false;
    }

    // --------------------------------------------------------
    // Tensor arena trong PSRAM
    // +16 để align
    // --------------------------------------------------------

    tensorArenaRaw =
        (uint8_t *)allocPSRAM(
            TENSOR_ARENA_SIZE + 16);

    if (!tensorArenaRaw)
    {
        Serial.println(
            "KHONG DU PSRAM CHO TENSOR ARENA!");

        return false;
    }

    uintptr_t aligned =
        ((uintptr_t)tensorArenaRaw +
         15) &
        ~(uintptr_t)15;

    tensorArena =
        (uint8_t *)aligned;

    // --------------------------------------------------------
    // Tôi đã kiểm tra model:
    //
    // Conv2D
    // MaxPool2D
    // DepthwiseConv2D
    // Mean
    // FullyConnected
    // Softmax
    // --------------------------------------------------------

    static tflite::MicroMutableOpResolver<6>
        resolver;

    // TensorFlow Lite 1.0.0 requires an error reporter in the interpreter.
    static tflite::ErrorReporter *errorReporter =
        tflite::GetMicroErrorReporter();

    resolver.AddConv2D();
    resolver.AddMaxPool2D();
    resolver.AddDepthwiseConv2D();
    resolver.AddMean();
    resolver.AddFullyConnected();
    resolver.AddSoftmax();

    static tflite::MicroInterpreter
        staticInterpreter(
            model,
            resolver,
            tensorArena,
            TENSOR_ARENA_SIZE,
            errorReporter);

    interpreter =
        &staticInterpreter;

    if (
        interpreter->AllocateTensors() != kTfLiteOk)
    {
        Serial.println(
            "AllocateTensors FAILED!");

        return false;
    }

    inputTensor =
        interpreter->input(0);

    outputTensor =
        interpreter->output(0);

    Serial.println();
    Serial.println(
        "======= MODEL =======");

    Serial.printf(
        "Model size: %u bytes\n",
        g_audio_model_data_5s_len);

    Serial.printf(
        "Input type: %d\n",
        inputTensor->type);

    Serial.print(
        "Input shape: ");

    for (
        int i = 0;
        i <
        inputTensor->dims->size;
        i++)
    {
        Serial.printf(
            "%d ",
            inputTensor
                ->dims
                ->data[i]);
    }

    Serial.println();

    Serial.printf(
        "Input scale: %.9f\n",
        inputTensor
            ->params
            .scale);

    Serial.printf(
        "Input zero: %d\n",
        inputTensor
            ->params
            .zero_point);

    Serial.print(
        "Output shape: ");

    for (
        int i = 0;
        i <
        outputTensor->dims->size;
        i++)
    {
        Serial.printf(
            "%d ",
            outputTensor
                ->dims
                ->data[i]);
    }

    Serial.println();
    Serial.println(
        "=====================");

    // --------------------------------------------------------
    // Kiểm tra đúng model bạn gửi
    // --------------------------------------------------------

    if (
        inputTensor->type !=
        kTfLiteInt8)
    {
        Serial.println(
            "ERROR: Model input khong phai INT8");

        return false;
    }

    if (
        inputTensor->dims->size != 4 ||
        inputTensor->dims->data[0] != 1 ||
        inputTensor->dims->data[1] != 40 ||
        inputTensor->dims->data[2] != 501 ||
        inputTensor->dims->data[3] != 1)
    {
        Serial.println(
            "ERROR: Input model khong phai [1,40,501,1]");

        return false;
    }

    if (
        outputTensor->type !=
        kTfLiteInt8)
    {
        Serial.println(
            "ERROR: Output khong phai INT8");

        return false;
    }

    int outputCount = 1;
    for (int i = 0; i < outputTensor->dims->size; ++i)
        outputCount *= outputTensor->dims->data[i];

    if (outputCount != 3)
    {
        Serial.printf("ERROR: Model hien tai phai co 3 output, dang co %d\n", outputCount);
        return false;
    }

    return true;
}

// ============================================================
// LOG-MEL -> INT8 MODEL
// ============================================================

void loadModelInput()
{
    const int count =
        N_MELS *
        N_FRAMES;

    float scale =
        inputTensor
            ->params
            .scale;

    int zeroPoint =
        inputTensor
            ->params
            .zero_point;

    for (
        int i = 0;
        i < count;
        i++)
    {
        float value =
            logMelBuffer[i];

        int32_t q =
            (int32_t)roundf(
                value / scale) +
            zeroPoint;

        if (q < -128)
            q = -128;

        if (q > 127)
            q = 127;

        inputTensor
            ->data
            .int8[i] =
            (int8_t)q;
    }
}

// ============================================================
// OUTPUT INT8 -> FLOAT
// ============================================================

float getProbability(int index)
{
    int32_t q =
        outputTensor
            ->data
            .int8[index];

    float value =
        (q -
         outputTensor
             ->params
             .zero_point) *
        outputTensor
            ->params
            .scale;

    if (value < 0.0f)
        value = 0.0f;

    if (value > 1.0f)
        value = 1.0f;

    return value;
}

// ============================================================
// QUYẾT ĐỊNH
// ============================================================

const char *printDecision(
    float pKhoc,
    float pDapPha,
    float pOther)
{
    float scores[3] = {
        pKhoc,
        pDapPha,
        pOther};

    int best = 0;

    if (scores[1] > scores[best])
        best = 1;

    if (scores[2] > scores[best])
        best = 2;

    float bestScore =
        scores[best];

    float secondScore = 0.0f;

    for (int i = 0; i < 3; i++)
    {
        if (
            i != best &&
            scores[i] > secondScore)
        {
            secondScore =
                scores[i];
        }
    }

    float margin =
        bestScore -
        secondScore;

    Serial.println();
    Serial.println(
        "==============================");

    Serial.printf(
        "KHOC       : %6.2f %%\n",
        pKhoc * 100.0f);

    Serial.printf(
        "DAP PHA    : %6.2f %%\n",
        pDapPha * 100.0f);

    Serial.printf(
        "OTHER      : %6.2f %%\n",
        pOther * 100.0f);

    Serial.println(
        "------------------------------");

    /*
     * CẢNH BÁO:
     * - KHÓC: pKhoc >= 20% => cảnh báo ngay, kể cả OTHER đang cao hơn.
     * - ĐẬP PHÁ: phải là class cao nhất, >= 70% và có margin >= 10%.
     *
     * Cooldown 10 giây ở phía dưới vẫn ngăn gửi trùng liên tục.
     */
    if (pKhoc >= KHOC_ALERT_THRESHOLD)
    {
        Serial.println(
            ">>> CANH BAO: KHOC >= 20% <<<");
        return "khoc";
    }

    if (
        best == IDX_DAP_PHA &&
        pDapPha >= DAP_PHA_ALERT_THRESHOLD &&
        margin >= MIN_MARGIN)
    {
        Serial.println(
            ">>> CANH BAO: DAP PHA <<<");
        return "dap_pha";
    }

    Serial.println(
        ">>> BO QUA - KHONG PHAI KHOC/DAP PHA <<<");

    Serial.println(
        "==============================");

    return nullptr;
}

// ============================================================
// NHẬN DIỆN
// ============================================================

// Ket noi ESP32 vao Wi-Fi gia dinh de truy cap web server trong cung mang.
bool connectToWiFi()
{
    WiFi.mode(WIFI_STA);
    WiFi.setSleep(false);
    WiFi.begin(WIFI_SSID, WIFI_PASSWORD);

    Serial.printf("[WiFi] Dang ket noi %s", WIFI_SSID);
    const uint32_t timeoutMs = 20000;
    const uint32_t startedAt = millis();

    while (WiFi.status() != WL_CONNECTED && millis() - startedAt < timeoutMs)
    {
        delay(500);
        Serial.print(".");
    }
    Serial.println();

    if (WiFi.status() != WL_CONNECTED)
    {
        Serial.printf("[WiFi] Ket noi that bai: %s\n", wifiStatusText(WiFi.status()));
        return false;
    }

    Serial.printf("[WiFi] Da ket noi, IP ESP32: %s\n", WiFi.localIP().toString().c_str());
    Serial.printf("[WiFi] Web server: %s\n", WEB_SERVER_URL);
    return true;
}

// Stream WAV tu snapshot 10 giay trong uploadAudioBuffer.
class WavStream : public Stream
{
public:
    WavStream(const int16_t *samples, size_t sampleCount)
        : samples_(samples), totalBytes_(44 + sampleCount * sizeof(int16_t)), position_(0)
    {
        memset(header_, 0, sizeof(header_));
        memcpy(header_, "RIFF", 4);
        write32(4, static_cast<uint32_t>(totalBytes_ - 8));
        memcpy(header_ + 8, "WAVEfmt ", 8);
        write32(16, 16);
        write16(20, 1);
        write16(22, 1);
        write32(24, SAMPLE_RATE);
        write32(28, SAMPLE_RATE * sizeof(int16_t));
        write16(32, sizeof(int16_t));
        write16(34, 16);
        memcpy(header_ + 36, "data", 4);
        write32(40, static_cast<uint32_t>(totalBytes_ - 44));
    }

    size_t totalSize() const { return totalBytes_; }
    int available() override { return static_cast<int>(totalBytes_ - position_); }
    int read() override
    {
        if (position_ >= totalBytes_)
            return -1;
        return byteAt(position_++);
    }
    int peek() override
    {
        if (position_ >= totalBytes_)
            return -1;
        return byteAt(position_);
    }
    void flush() override {}
    size_t write(uint8_t) override { return 0; }
    size_t readBytes(uint8_t *buffer, size_t length) override
    {
        size_t count = 0;
        while (count < length && position_ < totalBytes_)
            buffer[count++] = byteAt(position_++);
        return count;
    }

private:
    const int16_t *samples_;
    size_t totalBytes_;
    size_t position_;
    uint8_t header_[44];

    void write16(size_t offset, uint16_t value)
    {
        header_[offset] = value & 0xff;
        header_[offset + 1] = value >> 8;
    }
    void write32(size_t offset, uint32_t value)
    {
        for (int i = 0; i < 4; ++i)
            header_[offset + i] = (value >> (8 * i)) & 0xff;
    }
    uint8_t byteAt(size_t position) const
    {
        if (position < sizeof(header_))
            return header_[position];
        const uint8_t *pcm = reinterpret_cast<const uint8_t *>(samples_);
        return pcm[position - sizeof(header_)];
    }
};

String jsonStringValue(
    const String &json,
    const char *key)
{
    String marker = String("\"") + key + "\":\"";
    int start = json.indexOf(marker);

    if (start < 0)
        return "";

    start += marker.length();
    int end = json.indexOf('"', start);

    if (end < 0)
        return "";

    return json.substring(start, end);
}

bool uploadEvent(
    const char *label,
    float confidence,
    float pKhoc,
    float pDapPha,
    const String &eventId)
{
    String url = String(WEB_SERVER_URL) + "/api/recordings?device_id=" + DEVICE_ID;
    url += "&type=" + String(label);
    url += "&confidence=" + String(confidence, 4);
    url += "&khoc=" + String(pKhoc, 4);
    url += "&dap_pha=" + String(pDapPha, 4);

    url += "&event_id=" + eventId;

    for (uint8_t attempt = 1; attempt <= UPLOAD_ATTEMPTS; ++attempt)
    {
        // WavStream là stream một lần đọc, phải tạo lại sau mỗi lần retry.
        WavStream wav(uploadAudioBuffer, HISTORY_SAMPLES);
        HTTPClient http;
        int code = -1;
        String response;

        http.setConnectTimeout(10000);
        // Server local có thể đang lọc và ghi WAV nên cần chờ đủ lâu.
        http.setTimeout(60000);

        if (http.begin(url))
        {
            http.addHeader("Content-Type", "audio/wav");
            http.addHeader("X-Device-Id", DEVICE_ID);

            code = http.sendRequest(
                "POST",
                &wav,
                wav.totalSize());

            // Đọc response trước http.end() để biết đúng lỗi server local.
            response = http.getString();
            http.end();
        }
        else
        {
            Serial.println("[Web] Khong mo duoc ket noi HTTP.");
        }

        if (code >= 200 && code < 300)
        {
            Serial.printf(
                "[Web] Da luu server local - HTTP %d - lan thu %u\n",
                code,
                attempt);

            return true;
        }

        Serial.printf(
            "[Web] Upload loi - HTTP %d - lan thu %u/%u\n",
            code,
            attempt,
            UPLOAD_ATTEMPTS);

        if (response.length() > 0)
        {
            if (response.length() > 220)
                response = response.substring(0, 220);

            Serial.printf(
                "[Web] Server: %s\n",
                response.c_str());
        }

        if (attempt < UPLOAD_ATTEMPTS)
        {
            delay(UPLOAD_RETRY_DELAY_MS * attempt);
        }
    }

    Serial.println(
        "[Web] Khong upload duoc sau nhieu lan, khong danh dau cooldown.");
    return false;
}

// ============================================================
// SUY LUAN LIEN TUC
// ============================================================

bool runLatestInference(
    float &pKhoc,
    float &pDapPha,
    float &pOther)
{
    // Lay 5 giay moi nhat. Micro van tiep tuc thu o task rieng.
    snapshotLatestAudio(
        modelAudioBuffer,
        MODEL_SAMPLES);

    // Chi kiem tra muc am cua 1 giay gan nhat.
    const int16_t *latestOneSecond =
        modelAudioBuffer +
        (MODEL_SAMPLES - LEVEL_SAMPLES);

    const float dbfs =
        calculateRMSdBFS(
            latestOneSecond,
            LEVEL_SAMPLES);

    Serial.printf(
        "[Live] 1s gan nhat: %.1f dBFS\n",
        dbfs);

    if (dbfs < SOUND_TRIGGER_DBFS)
    {
        Serial.println(
            "[Live] Im/lang nhe -> bo qua AI.");
        return false;
    }

    const uint32_t start = millis();

    if (!calculateLogMel())
    {
        Serial.println("LOG-MEL FAILED!");
        return false;
    }

    loadModelInput();

    if (interpreter->Invoke() != kTfLiteOk)
    {
        Serial.println("INFERENCE FAILED!");
        return false;
    }

    pKhoc = getProbability(IDX_KHOC);
    pDapPha = getProbability(IDX_DAP_PHA);
    pOther = getProbability(IDX_OTHER);

    Serial.printf(
        "[Live] AI time: %lu ms\n",
        millis() - start);

    return true;
}

bool cooldownAllowsUpload(const char *label)
{
    const uint32_t now = millis();

    uint32_t *last =
        (strcmp(label, "khoc") == 0)
            ? &lastKhocUploadMs
            : &lastDapPhaUploadMs;

    bool *hasUploaded =
        (strcmp(label, "khoc") == 0)
            ? &hasKhocUpload
            : &hasDapPhaUpload;

    // Chỉ chặn theo lần upload thành công. Upload lỗi sẽ được thử lại.
    if (*hasUploaded &&
        now - *last < EVENT_COOLDOWN_MS)
    {
        Serial.printf(
            "[Web] Dang cooldown %lu ms, khong gui trung.\n",
            (unsigned long)(EVENT_COOLDOWN_MS -
                            (now - *last)));
        return false;
    }

    return true;
}

void markUploadSuccess(const char *label)
{
    if (strcmp(label, "khoc") == 0)
    {
        lastKhocUploadMs = millis();
        hasKhocUpload = true;
    }
    else
    {
        lastDapPhaUploadMs = millis();
        hasDapPhaUpload = true;
    }
}

bool tryPendingUpload()
{
    if (!pendingUpload)
        return true;

    Serial.printf(
        "[Web] Gui canh bao %s...\n",
        pendingLabel);

    if (uploadEvent(
            pendingLabel,
            pendingConfidence,
            pendingKhoc,
            pendingDapPha,
            pendingEventId))
    {
        markUploadSuccess(pendingLabel);
        pendingUpload = false;
        pendingEventId = "";
        Serial.println("[Web] Canh bao da luu server local.");
        return true;
    }

    Serial.printf(
        "[Web] Bo qua su kien %s sau %u lan loi; AI tiep tuc chay.\n",
        pendingLabel,
        UPLOAD_ATTEMPTS);

    // Khong khoa vong nhan dien vi mot su kien upload loi.
    // Am thanh/canh bao ke tiep van duoc xu ly binh thuong.
    pendingUpload = false;
    pendingEventId = "";

    return false;
}

void classifyAudioContinuous()
{
    static uint32_t lastInferenceMs = 0;
    static bool startupStabilizing = false;
    static uint32_t startupStableUntilMs = 0;
    static char candidateLabel[12] = {};
    static uint8_t candidateCount = 0;

    // Nếu upload lỗi, thử đủ 3 lần rồi quay lại nhận diện ngay.
    if (pendingUpload)
    {
        const bool uploaded = tryPendingUpload();
        if (!uploaded)
        {
            candidateLabel[0] = '\0';
            candidateCount = 0;
        }

        if (pendingUpload)
        {
            delay(10);
            return;
        }
    }

    uint64_t captured;

    portENTER_CRITICAL(&audioMux);
    captured = totalSamplesCaptured;
    portEXIT_CRITICAL(&audioMux);

    // Model 5 giay nen lan dau can co du 5 giay lich su.
    // Sau khi da warm-up, AI chay moi 1 giay, khong doi 10 giay.
    if (captured < (uint64_t)MODEL_SAMPLES)
    {
        static uint32_t lastWarmupPrint = 0;

        if (millis() - lastWarmupPrint >= 1000)
        {
            lastWarmupPrint = millis();

            float seconds =
                (float)captured /
                SAMPLE_RATE;

            Serial.printf(
                "[Live] Dang warm-up %.1f/5.0 giay...\n",
                seconds);
        }

        delay(20);
        return;
    }

    const uint32_t now = millis();

    if (!startupStabilizing)
    {
        startupStabilizing = true;
        startupStableUntilMs = now + STARTUP_STABILIZE_MS;
        candidateLabel[0] = '\0';
        candidateCount = 0;

        Serial.printf(
            "[Live] On dinh tin hieu dau phien trong %lu giay...\n",
            STARTUP_STABILIZE_MS / 1000);
    }

    if ((int32_t)(now - startupStableUntilMs) < 0)
    {
        delay(20);
        return;
    }

    if (now - lastInferenceMs < INFERENCE_INTERVAL_MS)
    {
        delay(10);
        return;
    }

    lastInferenceMs = now;

    float pKhoc = 0.0f;
    float pDapPha = 0.0f;
    float pOther = 0.0f;

    if (!runLatestInference(
            pKhoc,
            pDapPha,
            pOther))
    {
        return;
    }

    const char *eventLabel =
        printDecision(
            pKhoc,
            pDapPha,
            pOther);

    if (!eventLabel)
    {
        candidateLabel[0] = '\0';
        candidateCount = 0;
        return;
    }

    // Chong bao nhay: phai co cung nhan o 2 lan suy luan lien tiep.
    if (strcmp(candidateLabel, eventLabel) == 0)
    {
        if (candidateCount < ALERT_CONFIRMATIONS_REQUIRED)
            candidateCount++;
    }
    else
    {
        strlcpy(candidateLabel, eventLabel, sizeof(candidateLabel));
        candidateCount = 1;
    }

    if (candidateCount < ALERT_CONFIRMATIONS_REQUIRED)
    {
        Serial.printf(
            "[Live] Cho xac nhan %s (%u/%u)\n",
            eventLabel,
            candidateCount,
            ALERT_CONFIRMATIONS_REQUIRED);
        return;
    }

    // Chi bao tren Serial; khong gui WAV len server local.
    Serial.printf(
        "[Live] CANH BAO DA XAC NHAN: %s\n",
        eventLabel);

    return;

    // Canh bao hien tren Serial NGAY khi AI nhan ra.
    // Viec upload co cooldown de khong spam server.
    if (!cooldownAllowsUpload(eventLabel))
        return;

    // Snapshot 10 giay moi nhat de nghe tren web.
    // Neu he thong chua chay du 10 giay, dau file se duoc zero-pad.
    snapshotLatestAudio(
        uploadAudioBuffer,
        HISTORY_SAMPLES);

    const float confidence =
        strcmp(eventLabel, "khoc") == 0
            ? pKhoc
            : pDapPha;

    strlcpy(
        pendingLabel,
        eventLabel,
        sizeof(pendingLabel));

    pendingConfidence = confidence;
    pendingKhoc = pKhoc;
    pendingDapPha = pDapPha;
    pendingEventId = (String(DEVICE_ID) + "_" + String(millis()) + "_" + String(++eventSequence));
    pendingUpload = true;

    // Thử ngay; nếu đủ 3 lần lỗi, tryPendingUpload() trả quyền cho AI.
    if (!tryPendingUpload())
    {
        candidateLabel[0] = '\0';
        candidateCount = 0;
        Serial.println("[Live] Da reset xac nhan, tiep tuc tu dau.");
    }
}

void setup()
{
    Serial.begin(115200);

    Serial.println("[System] Che do doc lap: khong ket noi Wi-Fi/server.");

    delay(2000);

    Serial.println();
    Serial.println(
        "================================");

    Serial.println(
        " ESP32-S3 AUDIO CLASSIFICATION");

    Serial.println(
        " LIVE: KHOC / DAP PHA / BINH THUONG");

    Serial.println(
        "================================");

    // --------------------------------------------------------
    // Kiểm tra PSRAM
    // --------------------------------------------------------

    if (!psramFound())
    {
        Serial.println(
            "LOI: KHONG TIM THAY PSRAM!");

        Serial.println(
            "Model nay nen dung ESP32-S3 co PSRAM.");

        while (true)
        {
            delay(1000);
        }
    }

    Serial.printf(
        "PSRAM: %.2f MB\n",
        ESP.getPsramSize() /
            1024.0f /
            1024.0f);

    // --------------------------------------------------------
    // Audio buffers:
    // - ring 10 giay de thu lien tuc
    // - model 5 giay de inference
    // - upload 10 giay de gui WAV
    // --------------------------------------------------------

    audioRingBuffer =
        (int16_t *)allocPSRAM(
            HISTORY_SAMPLES *
            sizeof(int16_t));

    modelAudioBuffer =
        (int16_t *)allocPSRAM(
            MODEL_SAMPLES *
            sizeof(int16_t));

    uploadAudioBuffer =
        (int16_t *)allocPSRAM(
            HISTORY_SAMPLES *
            sizeof(int16_t));

    // --------------------------------------------------------
    // LogMel
    // --------------------------------------------------------

    logMelBuffer =
        (float *)allocPSRAM(
            N_MELS *
            N_FRAMES *
            sizeof(float));

    // --------------------------------------------------------
    // Mel weights
    // --------------------------------------------------------

    melWeights =
        (float *)allocPSRAM(
            N_MELS *
            FFT_BINS *
            sizeof(float));

    if (
        !audioRingBuffer ||
        !modelAudioBuffer ||
        !uploadAudioBuffer ||
        !logMelBuffer ||
        !melWeights)
    {
        Serial.println(
            "KHONG DU PSRAM!");

        while (true)
        {
            delay(1000);
        }
    }

    Serial.println(
        "Buffers: OK");

    createHannWindow();
    createMelFilterBank();

    // --------------------------------------------------------
    // Microphone
    // --------------------------------------------------------

    if (!initMicrophone())
    {
        Serial.println(
            "MICROPHONE FAILED!");

        while (true)
        {
            delay(1000);
        }
    }

    // --------------------------------------------------------
    // Model
    // --------------------------------------------------------

    if (!initModel())
    {
        Serial.println(
            "MODEL FAILED!");

        while (true)
        {
            delay(1000);
        }
    }

    // Bat task thu am lien tuc tren core 0.
    // Loop/AI va HTTP chay doc lap, nen trong luc inference/upload
    // micro van tiep tuc ghi vao ring buffer 10 giay.
    BaseType_t taskResult =
        xTaskCreatePinnedToCore(
            audioCaptureTask,
            "audio_capture",
            4096,
            nullptr,
            3,
            &audioCaptureTaskHandle,
            0);

    if (taskResult != pdPASS)
    {
        Serial.println(
            "AUDIO CAPTURE TASK FAILED!");

        while (true)
        {
            delay(1000);
        }
    }

    Serial.println();
    Serial.println(
        "HE THONG SAN SANG.");
}

// ============================================================
// LOOP
// ============================================================

void loop()
{
    // AI chay cuon: sau 5 giay warm-up, moi 1 giay
    // se xem lai 5 giay am thanh moi nhat.
    classifyAudioContinuous();

    delay(5);
}
