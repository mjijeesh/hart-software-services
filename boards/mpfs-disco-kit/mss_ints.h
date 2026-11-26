#ifndef MSS_INTS_H
#define MSS_INTS_H

#ifdef __cplusplus
extern "C" {
#endif

#ifndef asm
#define asm __asm__
#endif

/* Map legacy variable names to MPFS 32-bit window pointers */
#define g_mss_spi0 g_mss_spi0_lo
#define g_mss_spi1 g_mss_spi1_lo

/* Dummy interrupt IDs for polled driver mode */
#define SPI0_PLIC 0
#define SPI1_PLIC 0

#ifdef __cplusplus
}
#endif

#endif /* MSS_INTS_H */
