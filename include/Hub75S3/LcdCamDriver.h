#pragma once

#include <cstdint>
#include <esp_err.h>
#include <esp_log.h>
#include <esp_check.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <driver/gpio.h>
#include <esp_rom_gpio.h>
#include <esp_private/gdma.h>
#include <esp_private/periph_ctrl.h>
#include <hal/dma_types.h>
#include <hal/gpio_hal.h>
#include <soc/lcd_cam_struct.h>
#include <soc/lcd_cam_reg.h>
#include <soc/lcd_periph.h>
#include <esp_heap_caps.h>
#include <rom/ets_sys.h>
#include "Config.h"
#include "PinMap.h"

namespace Hub75S3 {

template<
    uint16_t PanelWidth,
    uint16_t PanelHeight,
    uint16_t ChainX,
    uint16_t ChainY,
    typename PinMap,
    uint8_t BcmBitDepth,
    uint8_t ScanRows
>
class LcdCamDriver {
public:
    static constexpr const char* TAG = "Hub75S3";
    static constexpr uint16_t TotalWidth = PanelWidth * ChainX;
    static constexpr uint16_t TotalHeight = PanelHeight * ChainY;
    static constexpr uint16_t PixelsPerRow = TotalWidth;
    static constexpr size_t BytesPerBitplaneRow = PixelsPerRow;
    static constexpr size_t TotalBitplaneSize = BytesPerBitplaneRow * ScanRows * BcmBitDepth;
    static constexpr size_t DescriptorCount = ScanRows * BcmBitDepth;

    // Base OE pulse width in microseconds for LSB (bit 0).
    // Higher = more color depth but lower refresh rate.
    // At 1 us base, 8-bit BCM, 32 scan rows:
    //   refresh period ~ 32 * (255 + 8*shift_time) us
    static constexpr uint8_t BcmBaseUs = 1;

    // LCD_CAM pixel clock: 160 MHz / divider
    static constexpr uint8_t LcdClkDiv = 10; // 16 MHz pixel clock

    LcdCamDriver() = default;
    ~LcdCamDriver() { stop(); }

    LcdCamDriver(const LcdCamDriver&) = delete;
    LcdCamDriver& operator=(const LcdCamDriver&) = delete;

    esp_err_t init(const PinMapDef& pins) {
        pins_ = pins;

        // Enable LCD_CAM peripheral clock
        periph_module_enable(PERIPH_LCD_CAM_MODULE);

        // Configure LCD_CAM for i80 parallel output
        configureLcdCam();

        // Map HUB75 data pins (R1,G1,B1,R2,G2,B2) to LCD_DATA_OUT[0..5]
        // Map CLK to LCD_PCLK
        configureDataPins();

        // Configure address lines (A-E) and control pins (LAT, OE) as GPIO
        configureControlPins();

        // Allocate GDMA TX channel and connect to LCD_CAM
        esp_err_t err = allocateGdma();
        if (err != ESP_OK) {
            ESP_LOGE(TAG, "GDMA allocation failed: %s", esp_err_to_name(err));
            return err;
        }

        // Allocate DMA-capable bitplane buffer in internal SRAM
        bitplaneBuffer_ = static_cast<uint8_t*>(
            heap_caps_calloc(TotalBitplaneSize, 1, MALLOC_CAP_DMA | MALLOC_CAP_8BIT)
        );
        if (!bitplaneBuffer_) {
            ESP_LOGE(TAG, "Failed to allocate bitplane buffer (%u bytes)",
                     static_cast<unsigned>(TotalBitplaneSize));
            return ESP_ERR_NO_MEM;
        }

        // Allocate and build DMA descriptors (one per bitplane row)
        dmaDescs_ = static_cast<dma_descriptor_t*>(
            heap_caps_calloc(DescriptorCount, sizeof(dma_descriptor_t),
                             MALLOC_CAP_DMA | MALLOC_CAP_8BIT)
        );
        if (!dmaDescs_) {
            ESP_LOGE(TAG, "Failed to allocate DMA descriptors");
            return ESP_ERR_NO_MEM;
        }
        buildDmaDescriptors();

        ESP_LOGI(TAG, "Initialized: %ux%u, %u scan rows, %u-bit BCM, %u MHz pixel clock",
                 TotalWidth, TotalHeight, ScanRows, BcmBitDepth, 160 / LcdClkDiv);

        return ESP_OK;
    }

    esp_err_t startRefreshTask(uint8_t priority = 5, BaseType_t coreId = 1) {
        if (refreshTask_) return ESP_ERR_INVALID_STATE;

        BaseType_t ret = xTaskCreatePinnedToCore(
            refreshTaskFunc,
            "hub75_refresh",
            4096,
            this,
            priority,
            &refreshTask_,
            coreId
        );

        return (ret == pdPASS) ? ESP_OK : ESP_ERR_NO_MEM;
    }

    void stop() {
        running_ = false;
        if (refreshTask_) {
            // Give the task time to exit its loop
            vTaskDelay(pdMS_TO_TICKS(50));
            vTaskDelete(refreshTask_);
            refreshTask_ = nullptr;
        }
        if (dmaChan_) {
            gdma_disconnect(dmaChan_);
            gdma_del_channel(dmaChan_);
            dmaChan_ = nullptr;
        }
        if (bitplaneBuffer_) {
            heap_caps_free(bitplaneBuffer_);
            bitplaneBuffer_ = nullptr;
        }
        if (dmaDescs_) {
            heap_caps_free(dmaDescs_);
            dmaDescs_ = nullptr;
        }
        periph_module_disable(PERIPH_LCD_CAM_MODULE);
    }

    uint8_t* bitplaneBuffer() { return bitplaneBuffer_; }
    const uint8_t* bitplaneBuffer() const { return bitplaneBuffer_; }

    void setBrightness(uint8_t brightness) { brightness_ = brightness; }
    uint8_t brightness() const { return brightness_; }

private:
    // ── LCD_CAM peripheral configuration ──────────────────────────────

    void configureLcdCam() {
        // Reset
        LCD_CAM.lcd_user.lcd_reset = 1;
        LCD_CAM.lcd_user.lcd_reset = 0;

        // Clock: PLL_F160M source, divided down
        LCD_CAM.lcd_clock.val = 0;
        LCD_CAM.lcd_clock.clk_en = 1;
        LCD_CAM.lcd_clock.lcd_clk_sel = 3;            // PLL_F160M_CLK
        LCD_CAM.lcd_clock.lcd_clkm_div_num = LcdClkDiv;
        LCD_CAM.lcd_clock.lcd_clkm_div_a = 0;
        LCD_CAM.lcd_clock.lcd_clkm_div_b = 0;
        LCD_CAM.lcd_clock.lcd_clk_equ_sysclk = 0;
        LCD_CAM.lcd_clock.lcd_ck_idle_edge = 0;        // CLK idles low
        LCD_CAM.lcd_clock.lcd_ck_out_edge = 0;         // Data valid on rising edge

        // i80 mode, 8-bit bus
        LCD_CAM.lcd_ctrl.lcd_rgb_mode_en = 0;          // i80, not RGB mode

        LCD_CAM.lcd_user.lcd_2byte_en = 0;             // 8-bit bus width
        LCD_CAM.lcd_user.lcd_cmd = 0;                  // No command phase
        LCD_CAM.lcd_user.lcd_dummy = 0;                // No dummy phase
        LCD_CAM.lcd_user.lcd_dout = 1;                 // Data output enabled
        LCD_CAM.lcd_user.lcd_dout_cyclelen = PixelsPerRow - 1;
        LCD_CAM.lcd_user.lcd_always_out_en = 1;
        LCD_CAM.lcd_user.lcd_8bits_order = 0;
        LCD_CAM.lcd_user.lcd_bit_order = 0;
        LCD_CAM.lcd_user.lcd_byte_order = 0;

        LCD_CAM.lcd_misc.lcd_bk_en = 1;                // Bus clock enable

        // Reset async FIFO
        LCD_CAM.lcd_misc.lcd_afifo_reset = 1;
        LCD_CAM.lcd_misc.lcd_afifo_reset = 0;

        // Clear and disable all LCD interrupts
        LCD_CAM.lc_dma_int_clr.val = LCD_CAM.lc_dma_int_st.val;
        LCD_CAM.lc_dma_int_ena.val = 0;

        LCD_CAM.lcd_user.lcd_update = 1;
    }

    // ── GPIO matrix: data pins → LCD_CAM, control pins → GPIO ────────

    void configureDataPins() {
        // Map R1,G1,B1,R2,G2,B2 → LCD_DATA_OUT[0..5]
        const int8_t dataPins[6] = {
            pins_.r1, pins_.g1, pins_.b1,
            pins_.r2, pins_.g2, pins_.b2
        };

        for (int i = 0; i < 6; i++) {
            gpio_set_direction(static_cast<gpio_num_t>(dataPins[i]), GPIO_MODE_OUTPUT);
            esp_rom_gpio_connect_out_signal(
                dataPins[i],
                lcd_periph_i80_signals.buses[0].data_sigs[i],
                false, false
            );
        }

        // Map CLK → LCD WR strobe (acts as pixel clock in i80 mode)
        gpio_set_direction(static_cast<gpio_num_t>(pins_.clk), GPIO_MODE_OUTPUT);
        esp_rom_gpio_connect_out_signal(
            pins_.clk,
            lcd_periph_i80_signals.buses[0].wr_sig,
            false, false
        );
    }

    void configureControlPins() {
        // Address lines: direct GPIO output
        const int8_t addrPins[] = { pins_.a, pins_.b, pins_.c, pins_.d, pins_.e };
        for (auto pin : addrPins) {
            if (pin >= 0) {
                gpio_set_direction(static_cast<gpio_num_t>(pin), GPIO_MODE_OUTPUT);
                gpio_set_level(static_cast<gpio_num_t>(pin), 0);
            }
        }

        // LAT: starts low
        gpio_set_direction(static_cast<gpio_num_t>(pins_.lat), GPIO_MODE_OUTPUT);
        gpio_set_level(static_cast<gpio_num_t>(pins_.lat), 0);

        // OE: starts high (output disabled, active low)
        gpio_set_direction(static_cast<gpio_num_t>(pins_.oe), GPIO_MODE_OUTPUT);
        gpio_set_level(static_cast<gpio_num_t>(pins_.oe), 1);
    }

    // ── GDMA channel allocation ──────────────────────────────────────

    esp_err_t allocateGdma() {
        gdma_channel_alloc_config_t dma_cfg = {};
        dma_cfg.direction = GDMA_CHANNEL_DIRECTION_TX;

        esp_err_t err = gdma_new_ahb_channel(&dma_cfg, &dmaChan_);
        if (err != ESP_OK) return err;

        err = gdma_connect(dmaChan_, GDMA_MAKE_TRIGGER(GDMA_TRIG_PERIPH_LCD, 0));
        if (err != ESP_OK) return err;

        gdma_transfer_config_t trans_cfg = {};
        trans_cfg.max_data_burst_size = 0;
        trans_cfg.access_ext_mem = false;
        gdma_config_transfer(dmaChan_, &trans_cfg);

        // Register EOF callback — notifies the refresh task when DMA completes
        gdma_tx_event_callbacks_t cbs = {};
        cbs.on_trans_eof = dmaEofCb;
        gdma_register_tx_event_callbacks(dmaChan_, &cbs, this);

        return ESP_OK;
    }

    // ── DMA descriptors ──────────────────────────────────────────────

    void buildDmaDescriptors() {
        // One descriptor per bitplane row.
        // Layout in bitplaneBuffer_: [scan0_bit0][scan0_bit1]...[scan0_bitN][scan1_bit0]...
        for (size_t i = 0; i < DescriptorCount; i++) {
            dmaDescs_[i].dw0.owner = 1;                    // Owned by DMA
            dmaDescs_[i].dw0.suc_eof = 1;                  // EOF after this descriptor
            dmaDescs_[i].dw0.size = BytesPerBitplaneRow;
            dmaDescs_[i].dw0.length = BytesPerBitplaneRow;
            dmaDescs_[i].buffer = bitplaneBuffer_ + (i * BytesPerBitplaneRow);
            dmaDescs_[i].next = nullptr;                    // Single descriptor per transfer
        }
    }

    // ── DMA transfer ─────────────────────────────────────────────────

    // Returns the descriptor index for a given scan row and BCM bit
    static constexpr size_t descIndex(uint8_t scan, uint8_t bit) {
        return static_cast<size_t>(scan) * BcmBitDepth + bit;
    }

    void startDmaTransfer(uint8_t scan, uint8_t bit) {
        size_t idx = descIndex(scan, bit);

        // Re-arm the descriptor (DMA clears owner after transfer)
        dmaDescs_[idx].dw0.owner = 1;
        dmaDescs_[idx].dw0.suc_eof = 1;

        // Reset async FIFO before each transfer
        LCD_CAM.lcd_misc.lcd_afifo_reset = 1;
        LCD_CAM.lcd_misc.lcd_afifo_reset = 0;

        // Point GDMA at this descriptor and start
        gdma_reset(dmaChan_);
        gdma_start(dmaChan_, reinterpret_cast<intptr_t>(&dmaDescs_[idx]));

        // Trigger LCD_CAM to begin clocking out data
        LCD_CAM.lcd_user.lcd_update = 1;
        LCD_CAM.lcd_user.lcd_start = 1;
    }

    void waitDmaComplete() {
        // Block until the GDMA EOF ISR fires
        ulTaskNotifyTake(pdTRUE, pdMS_TO_TICKS(10));
    }

    // GDMA EOF ISR — fires when a DMA transaction completes
    static IRAM_ATTR bool dmaEofCb(gdma_channel_handle_t chan,
                                    gdma_event_data_t* event,
                                    void* ctx) {
        auto* self = static_cast<LcdCamDriver*>(ctx);
        BaseType_t woken = pdFALSE;
        vTaskNotifyGiveFromISR(self->refreshTask_, &woken);
        return woken == pdTRUE;
    }

    // ── Row addressing ───────────────────────────────────────────────

    void setRowAddress(uint8_t row) {
        gpio_set_level(static_cast<gpio_num_t>(pins_.a), (row >> 0) & 1);
        gpio_set_level(static_cast<gpio_num_t>(pins_.b), (row >> 1) & 1);
        gpio_set_level(static_cast<gpio_num_t>(pins_.c), (row >> 2) & 1);
        gpio_set_level(static_cast<gpio_num_t>(pins_.d), (row >> 3) & 1);
        if constexpr (ScanRows > 16) {
            gpio_set_level(static_cast<gpio_num_t>(pins_.e), (row >> 4) & 1);
        }
    }

    void latch() {
        gpio_set_level(static_cast<gpio_num_t>(pins_.lat), 1);
        gpio_set_level(static_cast<gpio_num_t>(pins_.lat), 0);
    }

    // ── BCM timing ───────────────────────────────────────────────────

    // OE pulse duration for a given BCM bit, scaled by brightness.
    // bit 0 (LSB) = BcmBaseUs, bit N = BcmBaseUs * 2^N
    // brightness scales linearly: duration * brightness / 255
    void bcmDelay(uint8_t bit) const {
        uint32_t duration_us = static_cast<uint32_t>(BcmBaseUs) << bit;
        duration_us = (duration_us * brightness_) / 255;
        if (duration_us > 0) {
            ets_delay_us(duration_us);
        }
    }

    // ── Refresh task ─────────────────────────────────────────────────

    static void refreshTaskFunc(void* param) {
        auto* self = static_cast<LcdCamDriver*>(param);
        self->running_ = true;

        while (self->running_) {
            for (uint8_t scan = 0; scan < ScanRows; ++scan) {
                self->setRowAddress(scan);

                for (uint8_t bit = 0; bit < BcmBitDepth; ++bit) {
                    // Blank output during data shift
                    gpio_set_level(static_cast<gpio_num_t>(self->pins_.oe), 1);

                    // Shift bitplane row data into the panel via DMA
                    self->startDmaTransfer(scan, bit);
                    self->waitDmaComplete();

                    // Latch the shifted data
                    self->latch();

                    // Enable output for bit-weighted duration
                    gpio_set_level(static_cast<gpio_num_t>(self->pins_.oe), 0);
                    self->bcmDelay(bit);
                    gpio_set_level(static_cast<gpio_num_t>(self->pins_.oe), 1);
                }
            }
        }

        vTaskDelete(nullptr);
    }

    // ── State ────────────────────────────────────────────────────────

    PinMapDef pins_{};
    uint8_t* bitplaneBuffer_ = nullptr;
    dma_descriptor_t* dmaDescs_ = nullptr;
    gdma_channel_handle_t dmaChan_ = nullptr;
    TaskHandle_t refreshTask_ = nullptr;
    volatile bool running_ = false;
    volatile uint8_t brightness_ = 255;
};

} // namespace Hub75S3
