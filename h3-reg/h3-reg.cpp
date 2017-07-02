#include <stdio.h>
#include <stdint.h>
#include <sys/mman.h>
#include <errno.h>
#include <string.h>
#include <unistd.h>

#include <sys/stat.h>
#include <fcntl.h>

#include <bitset>
#include <iostream>


#define DA0_PAGE 0x01C22000
#define DA0_BASE 0x01C22000
#define DA1_BASE 0x01C22000
#define DA0_OFFSET (DA0_BASE - DA0_PAGE)
#define DA1_OFFSET (DA1_BASE - DA0_PAGE)
#define DA0_LEN  (0x40+DA0_OFFSET)

#define CTL_OFFSET  (0x00)
#define FMT0_OFFSET (0x04)
#define FMT1_OFFSET (0x08)


#define DAC_PAGE 0x01C22000
#define DAC_BASE 0x01C22C00
#define DAC_OFFSET (DAC_BASE - DAC_PAGE)
#define DAC_LEN (DAC_OFFSET + 0x40)

#define AC_DAC_DPC_OFFSET  (0x00)
#define AC_DAC_FIFOC_OFFSET (0x04)
#define AC_DAC_FIFOS_OFFSET (0x08)

template<uint32_t N>
uint32_t extract_value(const std::bitset<N> the_bitset, const uint32_t start_bit, const uint32_t end_bit) {
	unsigned long mask = 1;
	unsigned long result = 0;
	for (size_t i = start_bit; i < end_bit; ++ i) {
		if (the_bitset.test(i))
			result |= mask;
		mask <<= 1;
	}
	return (uint32_t)result;
}

template<uint32_t N>
void dump_value(const std::bitset<N> bs, 
                const char * name, const int start, const size_t len)
{
	uint32_t v = extract_value(bs, start, start+len);
	printf("\t%s: 0x%04X\n", name, v);
}

static unsigned char * init_mmap(off_t base, size_t len) {
	int fd = open("/dev/mem", O_RDWR|O_SYNC);

	if (fd < 0) {
		fprintf(stderr, "Could not open /dev/mem\n");
		return NULL;
	}

	void * ptr = mmap(NULL, len, PROT_READ|PROT_WRITE,
			 MAP_SHARED, fd, base);
	if ( MAP_FAILED == ptr ) {
		fprintf(stderr, "fd = %d, mmap error: %d-%s\n", fd, errno, strerror(errno));
		return NULL;
	}
	return static_cast<unsigned char*>(ptr);
}

int write_reg(off_t base, off_t off, uint32_t val) {
	printf("%x, %x, %x\n", base, off, val);
	unsigned char * page = init_mmap(base, 0x1000);
	printf("page = 0x%08X\n", page);
	uint32_t *pv = ((uint32_t*)(page + off));
	printf("val = 0x%08X\n", *pv);
	*pv = val;
	printf("val = 0x%08X\n", *pv);
	return munmap(page, 0x200);
}

int main(int argc, char ** argv) {
	if ( argc > 2 ) {
		off_t base = 0;
		off_t off = 0;
		uint32_t val = 0;
		sscanf(argv[1], "0x%08X", &base);
		sscanf(argv[2], "0x%04X", &off);
		sscanf(argv[3], "0x%08X", &val);
		return write_reg(base, off, val);
	}
	unsigned char * page;
	uint32_t   v;
	std::bitset<32> bs(v);
	/*
	page = init_mmap(DAC_PAGE, DAC_LEN);
	if ( NULL == page ) {
		return -1;
	}
	v = *((uint32_t*)(page + DAC_OFFSET + AC_DAC_DPC_OFFSET));
	bs=v;
	printf("AC_DAC_DPC: 0x%08X\n", v);
	dump_value(bs, "Digital Part Enable", 31, 1);
	dump_value(bs, "Internal DAC Quantization Levels", 25, 4);

	v = *((uint32_t*)(page + DAC_OFFSET + AC_DAC_FIFOC_OFFSET));
	bs = v;
	printf("AC_DAC_FIFOC: 0x%08X\n", v);
	dump_value(bs, "FIFO_FLUSH", 0, 1);
	dump_value(bs, "FIFO_OVERRUN_IRQ_EN", 1, 1);
	dump_value(bs, "FIFO_UNDERRUN_IRQ_EN", 2, 1);
	dump_value(bs, "DAC_IRQ_EN", 3, 1);
	dump_value(bs, "DAC_DRQ_EN", 4, 1);
	dump_value(bs, "TX_SAMPLE_BITS", 5, 1);
	dump_value(bs, "DAC_MONO_EN", 6, 1);
	dump_value(bs, "ADDA_LOOP_EN", 7, 1);
	dump_value(bs, "TX_TRIG_LEVEL", 8, 7);
	dump_value(bs, "DAC_DRQ_CLR_CNT", 21, 2);
	dump_value(bs, "FIFO_MODE", 24, 2);
	dump_value(bs, "SEND_LASAT", 26, 1);
	dump_value(bs, "Sample Rate of DAC", 29, 3);

	v = *((uint32_t*)(page + DAC_OFFSET + 0x08));
	bs = v;
	printf("AC_DAC_FIFOS: 0x%08X\n", v);
	dump_value(bs, "TXO_INT", 1, 1);
	dump_value(bs, "TXU_INT", 2, 1);
	dump_value(bs, "TXE_INT", 3, 1);
	dump_value(bs, "TXE_CNT", 8, 15);
	dump_value(bs, "TX_EMPTY", 23, 1);

	v = *((uint32_t*)(page + DAC_OFFSET + 0x0C));
	bs = v;
	printf("AC_DAC_TXDATA: 0x%08X\n", v);

	v = *((uint32_t*)(page + DAC_OFFSET + 0x10));
	bs = v;
	printf("AC_DAC_ACTRL: 0x%08X\n", v);

	dump_value(bs, "PAVOL", 0, 6);
	dump_value(bs, "PAMUTE", 6, 1);
	dump_value(bs, "MIXPAS", 7, 1);
	dump_value(bs, "DAC to PA Mute", 8, 1);
	dump_value(bs, "Left DAC to right output mixer Mute", 13, 1);
	dump_value(bs, "Right DAC to right output mixer Mute", 14, 1);
	dump_value(bs, "Left DAC to left output mixer Mute", 15, 1);
	dump_value(bs, "Analog Output Mixer Enable", 29, 1);
	dump_value(bs, "Internal DAC Analog Left channel Enable", 30, 1);
	dump_value(bs, "Internal DAC Analog Right channel Enable", 31, 1);

	munmap(page, DAC_LEN);
	*/

	page = init_mmap(0x01C20000, 0x200);
	if ( NULL == page ) {
		return -1;
	}
	v = *((uint32_t*)(page + 0x0008));
	bs = v;
	printf("PLL2_CFG_REG: 0x%08X, page = %p\n", v, page);
	dump_value(bs, "PLL2_PRE_DIV", 0, 4);
	dump_value(bs, "PLL2_Factor_N", 8, 7);
	dump_value(bs, "PLL2_POST_DIV", 26, 4);
	dump_value(bs, "PLL2_Enable", 31, 1);

	v = *((uint32_t*)(page + 0x000C));
	bs = v;
	printf("PLL2_TUN_REG: 0x%08X\n", v);

	v = *((uint32_t*)(page + 0x00B8));
	bs = v;
	printf("IIS0_CLK_REG: 0x%08X\n", v);
	dump_value(bs, "CLK_SRC_SEL", 16, 2);
	dump_value(bs, "Enabled", 31, 1);

	v = *((uint32_t*)(page + 0x140));
	bs = v;
	printf("AUDIO_CODEC_CLK_REG: 0x%08X\n", v);
	dump_value(bs, "Gating Special Clock", 31, 1);


	munmap(page, 0x200);

	page = init_mmap(0x01C22000, 0x500);
	if ( NULL == page ) {
		return -1;
	}
	v = *((uint32_t*)(page + 0x000 + 0x0000));
	bs = v;
	printf("DA0 DA_CTL: 0x%08X\n", v);
	dump_value(bs, "Globe Enable", 0, 1);
	dump_value(bs, "Receiver Block Enable", 1, 1);
	dump_value(bs, "Transmitter Block Enable", 2, 1);
	dump_value(bs, "LOOP MODE", 3, 1);
	dump_value(bs, "MODE SEL", 4, 2);
	dump_value(bs, "MUTE", 6, 1);
	dump_value(bs, "SDO0_EN", 8, 1);
	dump_value(bs, "LRCKR_OUT", 16, 1);
	dump_value(bs, "LRCK_OUT", 17, 1);
	dump_value(bs, "BCLK_OUT", 18, 1);

	v = *((uint32_t*)(page + 0x000 + 0x0004));
	bs = v;
	printf("DA0 FMT0: 0x%08X\n", v);
	dump_value(bs, "SW", 0, 3);
	dump_value(bs, "EDGE_TRANSFER", 3, 1);
	dump_value(bs, "SR", 4, 3);
	dump_value(bs, "BCLK_POLARITY", 7, 1);
	dump_value(bs, "LRCK_PERIOD", 8, 10);
	dump_value(bs, "LRCKR_POLARITY", 19, 1);
	dump_value(bs, "LRCKR_PERIOD", 20, 10);
	dump_value(bs, "LRCK_WIDTH", 30, 1);
	dump_value(bs, "SDI_SYNC_SEL", 31, 1);
	v = *((uint32_t*)(page + 0x000 + 0x0024));
	bs = v;
	printf("DA0 DA_CLKD: 0x%08X\n", v);
	dump_value(bs, "MCLK Divide Ratio from Audio PLL Output", 0, 4);
	dump_value(bs, "BCLK Divide Ratio from MCLK", 4, 3);
	dump_value(bs, "MCLKO_EN", 8, 1);

	v = *((uint32_t*)(page + 0x000 + 0x0030));
	bs = v;
	printf("DA0 DA_TXCHSEL: 0x%08X\n", v);
	dump_value(bs, "TX_SLOT_NUM", 0, 3);

	for ( int i = 0; i < 4; i++ ) {
		v = *((uint32_t*)(page + 0x000 + 0x0034+i*4));
		bs = v;
		printf("PCM_TXnSEL %d: 0x%08X\n", i, v);
	}
	for ( int i = 0; i < 4; i++ ) {
		v = *((uint32_t*)(page + 0x000 + 0x0044+i*4));
		bs = v;
		printf("PCM_TXnCHMAP %d: 0x%08X\n", i, v);
	}

	return 0;
}
