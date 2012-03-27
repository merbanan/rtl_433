#ifndef __I2C_H
#define __I2C_H

typedef int rtlsdr_dev_t;

int rtlsdr_i2c_write(rtlsdr_dev_t *dev, uint8_t i2c_addr, uint8_t *buffer, int len);
int rtlsdr_i2c_read(rtlsdr_dev_t *dev, uint8_t i2c_addr, uint8_t *buffer, int len);

#endif
