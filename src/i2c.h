#ifndef __I2C_H
#define __I2C_H

int rtl_i2c_write(uint8_t i2c_addr, uint8_t *buffer, int len);
int rtl_i2c_read(uint8_t i2c_addr, uint8_t *buffer, int len);

#endif
