#include "mpu6050.h"

float accel_range = 2.0;
vec3_t gravity_offset = { 0.0, 0.0, -10.0 };
vec3_t fwd_dir = { 1.0, 0.0, 0.0 };

void mpu6050_task(void *p) {

    mpu6050_task_arg_t *arg = p;
    float *speed_ptr = &(arg->control_state->vel);
    QueueHandle_t accel_queue = arg->accel_queue;

    // init mpu6050
    mpu6050_activate();
    mpu6050_set_dlpf(MPU6050_DLPF_94HZ);
    mpu6050_set_range(MPU6050_ACCEL_RANGE_8G);

    vec3_t accel;

    while (1) {
        mpu6050_get_accel(&accel);
        xQueueOverwrite(accel_queue, &accel);
        vTaskDelay(pdMS_TO_TICKS(50));
    }
}

bool mpu6050_command(uint8_t reg, uint8_t val) {
    uint8_t buf[] = {reg, val};
    int status = i2c_write_timeout_us(
        I2C_INSTANCE,
        MPU6050_ADDR,
        buf,
        2,
        false,
        100 * 1000
    );
    return status != PICO_ERROR_GENERIC || status != PICO_ERROR_TIMEOUT;
}

void mpu6050_activate() {
    mpu6050_command(MPU6050_PWR_MGMT_1, 0x00);
}

void mpu6050_set_dlpf(uint8_t freq) {
    mpu6050_command(MPU6050_CONFIG, freq & 0b111);
}

void mpu6050_set_range(uint8_t range) {
    switch (range) {
        case MPU6050_ACCEL_RANGE_2G:
            accel_range = 2.0;
            break;
        case MPU6050_ACCEL_RANGE_4G:
            accel_range = 4.0;
            break;
        case MPU6050_ACCEL_RANGE_8G:
            accel_range = 8.0;
            break;
        case MPU6050_ACCEL_RANGE_16G:
            accel_range = 16.0;
            break;
        default:
            accel_range = 2.0;
    }
    mpu6050_command(MPU6050_ACCEL_CONFIG, range & (0b11 << 3));
}

void mpu6050_get_accel(vec3_t *out) {
    uint8_t buf[6];
    // Read accelerometer
    uint8_t reg = MPU6050_ACCEL_REG;
    i2c_write_timeout_us(i2c_default, MPU6050_ADDR, &reg, 1, true, 100 * 1000);
    i2c_read_timeout_us(i2c_default, MPU6050_ADDR, buf, 6, false, 100 * 1000);
    for (int i = 0; i < 3; i++) {
        int16_t raw = (buf[i * 2] << 8 | buf[(i * 2) + 1]);
        // convert to m/s/s
        out->v[i] = raw * 2.0 * accel_range * GS_TO_MSS / ((1<<16)-1);
    }
}

#define NUM_GRAV_READINGS 50
#define REC_FWD_MS 500
#define ACCEL_INTERVAL_MS 20
#define NUM_FWD_READINGS (REC_FWD_MS / ACCEL_INTERVAL_MS)
void mpu6050_calibrate() {
    // Gravity offset: mean of readings taken while stationary.

    vec3_t grav_buf[NUM_GRAV_READINGS];
    for (int i = 0; i < NUM_GRAV_READINGS; i++) {
        mpu6050_get_accel(grav_buf + i);
        sleep_ms(ACCEL_INTERVAL_MS);
    }
    vec_mean(grav_buf, NUM_GRAV_READINGS, &gravity_offset);
    
    // Forward direction: mean of readings taken during a period of
    // increasing forward acceleration.

    // TODO: automate car forward movement when recording

    vec3_t fwd_buf[NUM_FWD_READINGS];
    for (int i = 0; i < NUM_FWD_READINGS; i++) {
        mpu6050_get_accel(fwd_buf + i);
        sleep_ms(ACCEL_INTERVAL_MS);
    }
    vec_mean(fwd_buf, NUM_FWD_READINGS, &fwd_dir);
    // remove gravity offset
    vec_sub(&fwd_dir, &gravity_offset, &fwd_dir);
    // normalize
    vec_scalar_div(&fwd_dir, vec_mag(&fwd_dir), &fwd_dir);
}