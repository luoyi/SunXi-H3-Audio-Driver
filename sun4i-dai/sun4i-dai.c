/*
 * Copyright (C) 2015 Andrea Venturi
 * Andrea Venturi <be17068@iperbole.bo.it>
 *
 * Copyright (C) 2016 Maxime Ripard
 * Maxime Ripard <maxime.ripard@free-electrons.com>
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License as
 * published by the Free Software Foundation; either version 2 of
 * the License, or (at your option) any later version.
 */

#include <linux/clk.h>
#include <linux/dmaengine.h>
#include <linux/module.h>
#include <linux/of_device.h>
#include <linux/platform_device.h>
#include <linux/pm_runtime.h>
#include <linux/regmap.h>
#include <linux/reset.h>

#include <sound/dmaengine_pcm.h>
#include <sound/pcm_params.h>
#include <sound/soc.h>
#include <sound/soc-dai.h>

#define SUN4I_I2S_CTRL_REG		0x00
#define SUN4I_I2S_CTRL_BCLK_OUT			BIT(18)
#define SUN4I_I2S_CTRL_LRCK_OUT			BIT(17)
#define SUN4I_I2S_CTRL_LRCKR_OUT		BIT(16)
#define SUN4I_I2S_CTRL_SDO_EN_MASK		GENMASK(11, 8)
#define SUN4I_I2S_CTRL_SDO_EN(sdo)			BIT(8 + (sdo))
#define SUN4I_I2S_CTRL_MODE_MASK		BIT(5)
#define SUN8I_I2S_CTRL_MODE_MASK		GENMASK(5, 4)
#define SUN8I_I2S_CTRL_MODE_RIGHT_J			(2 << 4)
#define SUN8I_I2S_CTRL_MODE_I2S			(1 << 4)
#define SUN8I_I2S_CTRL_MODE_PCM			(0 << 4)
#define SUN4I_I2S_CTRL_MODE_SLAVE			(1 << 5)
#define SUN4I_I2S_CTRL_MODE_MASTER			(0 << 5)
#define SUN4I_I2S_CTRL_LOOP			BIT(3)
#define SUN4I_I2S_CTRL_TX_EN			BIT(2)
#define SUN4I_I2S_CTRL_RX_EN			BIT(1)
#define SUN4I_I2S_CTRL_GL_EN			BIT(0)

#define SUN4I_I2S_FMT0_REG		0x04
#define SUN8I_I2S_FMT0_LRCLK_POLARITY_MASK	BIT(19)
#define SUN8I_I2S_FMT0_LRCLK_POLARITY_INVERTED		(1 << 19)
#define SUN8I_I2S_FMT0_LRCLK_POLARITY_NORMAL		(0 << 19)
#define SUN8I_I2S_FMT0_LRCK_PERIOD_MASK		GENMASK(17, 8)
#define SUN8I_I2S_FMT0_LRCK_PERIOD(period)		((period) << 8)
#define SUN4I_I2S_FMT0_LRCLK_POLARITY_MASK	BIT(7)
#define SUN4I_I2S_FMT0_LRCLK_POLARITY_INVERTED		(1 << 7)
#define SUN4I_I2S_FMT0_LRCLK_POLARITY_NORMAL		(0 << 7)
#define SUN8I_I2S_FMT0_BCLK_POLARITY_MASK	BIT(7)
#define SUN8I_I2S_FMT0_BCLK_POLARITY_INVERTED		(1 << 7)
#define SUN8I_I2S_FMT0_BCLK_POLARITY_NORMAL		(0 << 7)
#define SUN4I_I2S_FMT0_BCLK_POLARITY_MASK	BIT(6)
#define SUN4I_I2S_FMT0_BCLK_POLARITY_INVERTED		(1 << 6)
#define SUN4I_I2S_FMT0_BCLK_POLARITY_NORMAL		(0 << 6)
#define SUN4I_I2S_FMT0_SR_MASK			GENMASK(5, 4)
#define SUN8I_I2S_FMT0_SR_MASK			GENMASK(6, 4)
#define SUN4I_I2S_FMT0_SR(sr)				((sr) << 4)
#define SUN4I_I2S_FMT0_WSS_MASK			GENMASK(3, 2)
#define SUN4I_I2S_FMT0_WSS(wss)				((wss) << 2)
#define SUN8I_I2S_FMT0_WSS_MASK			GENMASK(2, 0)
#define SUN8I_I2S_FMT0_WSS(wss)				(wss)
#define SUN4I_I2S_FMT0_FMT_MASK			GENMASK(1, 0)
#define SUN4I_I2S_FMT0_FMT_RIGHT_J			(2 << 0)
#define SUN4I_I2S_FMT0_FMT_LEFT_J			(1 << 0)
#define SUN4I_I2S_FMT0_FMT_I2S				(0 << 0)

#define SUN4I_I2S_FMT1_REG		0x08
#define SUN4I_I2S_FIFO_TX_REG		0x0c
#define SUN8I_I2S_INT_STA_REG		0x0c
#define SUN4I_I2S_FIFO_RX_REG		0x10

#define SUN4I_I2S_FIFO_CTRL_REG		0x14
#define SUN4I_I2S_FIFO_CTRL_FLUSH_TX		BIT(25)
#define SUN4I_I2S_FIFO_CTRL_FLUSH_RX		BIT(24)
#define SUN4I_I2S_FIFO_CTRL_TX_MODE_MASK	BIT(2)
#define SUN4I_I2S_FIFO_CTRL_TX_MODE(mode)		((mode) << 2)
#define SUN4I_I2S_FIFO_CTRL_RX_MODE_MASK	GENMASK(1, 0)
#define SUN4I_I2S_FIFO_CTRL_RX_MODE(mode)		(mode)

#define SUN4I_I2S_FIFO_STA_REG		0x18

#define SUN4I_I2S_DMA_INT_CTRL_REG	0x1c
#define SUN4I_I2S_DMA_INT_CTRL_TX_DRQ_EN	BIT(7)
#define SUN4I_I2S_DMA_INT_CTRL_RX_DRQ_EN	BIT(3)

#define SUN4I_I2S_INT_STA_REG		0x20
#define SUN8I_I2S_FIFO_TX_REG		0x20

#define SUN4I_I2S_CLK_DIV_REG		0x24
#define SUN8I_I2S_CLK_DIV_MCLK_EN		BIT(8)
#define SUN4I_I2S_CLK_DIV_MCLK_EN		BIT(7)
#define SUN4I_I2S_CLK_DIV_BCLK_MASK		GENMASK(6, 4)
#define SUN8I_I2S_CLK_DIV_BCLK_MASK		GENMASK(7, 4)
#define SUN4I_I2S_CLK_DIV_BCLK(bclk)			((bclk) << 4)
#define SUN4I_I2S_CLK_DIV_MCLK_MASK		GENMASK(3, 0)
#define SUN4I_I2S_CLK_DIV_MCLK(mclk)			((mclk) << 0)

#define SUN4I_I2S_RX_CNT_REG		0x28
#define SUN4I_I2S_TX_CNT_REG		0x2c

#define SUN4I_I2S_TX_CHAN_SEL_REG	0x30
#define SUN8I_I2S_TX_CHAN_CFG_REG	0x30
#define SUN4I_I2S_TX_CHAN_SEL(num_chan)		(((num_chan) - 1) << 0)

#define SUN4I_I2S_TX_CHAN_MAP_REG	0x34
#define SUN4I_I2S_TX_CHAN_MAP(chan, sample)	((sample) << (chan << 2))
#define SUN8I_I2S_TX_CHAN_SEL_REG	0x34
#define SUN8I_I2S_TX_CHAN_OFFSET_MASK		GENMASK(13, 12)
#define SUN8I_I2S_TX_CHAN_OFFSET(offset)	(offset << 12)
#define SUN8I_I2S_TX_CHAN_EN_MASK		GENMASK(11, 4)
#define SUN8I_I2S_TX_CHAN_EN(num_chan)		(((1 << num_chan) - 1) << 4)
#define SUN8I_I2S_TX_CHAN_SEL_MASK		GENMASK(2, 0)
#define SUN8I_I2S_TX_CHAN_SEL(num_chan)		(((num_chan) - 1) << 0)

#define SUN4I_I2S_RX_CHAN_SEL_REG	0x38
#define SUN4I_I2S_RX_CHAN_MAP_REG	0x3c

#define SUN8I_I2S_TX_CHAN_MAP_REG	0x44

#define SUN8I_I2S_RX_CHAN_SEL_REG      0x54
#define SUN8I_I2S_RX_CHAN_MAP_REG      0x58

struct sun4i_i2s {
	struct clk	*bus_clk;
	struct clk	*mod_clk;
	struct regmap	*regmap;
	struct reset_control *rst;

	unsigned int	mclk_freq;

	struct snd_dmaengine_dai_dma_data	capture_dma_data;
	struct snd_dmaengine_dai_dma_data	playback_dma_data;

	bool loopback;
};

struct sun4i_i2s_clk_div {
	u8	div;
	u8	val;
};

static const struct sun4i_i2s_clk_div sun4i_i2s_bclk_div[] = {
	{ .div = 2, .val = 0 },
	{ .div = 4, .val = 1 },
	{ .div = 6, .val = 2 },
	{ .div = 8, .val = 3 },
	{ .div = 12, .val = 4 },
	{ .div = 16, .val = 5 },
};

static const struct sun4i_i2s_clk_div sun4i_i2s_mclk_div[] = {
	{ .div = 1, .val = 0 },
	{ .div = 2, .val = 1 },
	{ .div = 4, .val = 2 },
	{ .div = 6, .val = 3 },
	{ .div = 8, .val = 4 },
	{ .div = 12, .val = 5 },
	{ .div = 16, .val = 6 },
	{ .div = 24, .val = 7 },
};


static const struct sun4i_i2s_clk_div sun4i_dai_bclk_div[] = {
	{ .div = 2, .val = 0 },
	{ .div = 4, .val = 1 },
	{ .div = 6, .val = 2 },
	{ .div = 8, .val = 3 },
	{ .div = 12, .val = 4 },
	{ .div = 16, .val = 5 },
	{ .div = 32, .val = 6 },
	{ .div = 64, .val = 7 },
	{ /* Sentinel */ },
};

static const struct sun4i_i2s_clk_div sun4i_dai_mclk_div[] = {
	{ .div = 1, .val = 0 },
	{ .div = 2, .val = 1 },
	{ .div = 4, .val = 2 },
	{ .div = 6, .val = 3 },
	{ .div = 8, .val = 4 },
	{ .div = 12, .val = 5 },
	{ .div = 16, .val = 6 },
	{ .div = 24, .val = 7 },
	{ .div = 32, .val = 8 },
	{ .div = 48, .val = 9 },
	{ .div = 64, .val = 10 },
	{ /* Sentinel */ },
};

static const struct sun4i_i2s_clk_div sun8i_dai_bclk_div[] = {
	{ .div = 1, .val = 1 },
	{ .div = 2, .val = 2 },
	{ .div = 4, .val = 3 },
	{ .div = 6, .val = 4 },
	{ .div = 8, .val = 5 },
	{ .div = 12, .val = 6 },
	{ .div = 16, .val = 7 },
	{ .div = 24, .val = 8 },
	{ .div = 32, .val = 9 },
	{ .div = 48, .val = 10 },
	{ .div = 64, .val = 11 },
	{ .div = 96, .val = 12 },
	{ .div = 128, .val = 13 },
	{ .div = 176, .val = 14 },
	{ .div = 192, .val = 15 },
	{ .div = 0, .val = 16 },
	{ /* Sentinel */ },
};

static const struct sun4i_i2s_clk_div sun8i_dai_mclk_div[] = {
	{ .div = 1, .val = 1 },
	{ .div = 2, .val = 2 },
	{ .div = 4, .val = 3 },
	{ .div = 6, .val = 4 },
	{ .div = 8, .val = 5 },
	{ .div = 12, .val = 6 },
	{ .div = 16, .val = 7 },
	{ .div = 24, .val = 8 },
	{ .div = 32, .val = 9 },
	{ .div = 48, .val = 10 },
	{ .div = 64, .val = 11 },
	{ .div = 96, .val = 12 },
	{ .div = 128, .val = 13 },
	{ .div = 176, .val = 14 },
	{ .div = 192, .val = 15 },
	{ .div = 0, .val = 16 },
	{ /* Sentinel */ },
};

static int sun4i_calc_bclk_mclk(unsigned int rate, unsigned int pll2, unsigned int wss,
		unsigned chan_num, unsigned int* pbclk, unsigned int * pmclk)
{
	int i;
	int j;

	unsigned int mb = pll2/(rate * wss * chan_num);

	for (i = 0; sun4i_dai_mclk_div[i].div; i++) {
		for (j = 0; sun4i_dai_bclk_div[j].div; j++) {
			unsigned int m = sun4i_dai_mclk_div[i].div;
			unsigned int b = sun4i_dai_bclk_div[j].div;
			if ( m * b == mb ) {
				*pbclk = sun4i_dai_bclk_div[j].val;
				*pmclk = sun4i_dai_mclk_div[i].val;
				return 0;
			}
		}
	}
	*pbclk = -1;
	*pmclk = -1;
	return -EINVAL;
}
static int sun8i_calc_bclk_mclk(unsigned int rate, unsigned int pll2, unsigned int wss,
		unsigned chan_num, unsigned int* pbclk, unsigned int * pmclk)
{
	int i;
	int j;

	unsigned int mb = pll2/(rate * wss * chan_num);

	for (i = 0; sun8i_dai_mclk_div[i].div; i++) {
		for (j = 0; sun8i_dai_bclk_div[j].div; j++) {
			unsigned int m = sun8i_dai_mclk_div[i].div;
			unsigned int b = sun8i_dai_bclk_div[j].div;
			if ( m * b == mb ) {
				*pbclk = sun8i_dai_bclk_div[j].val;
				*pmclk = sun8i_dai_mclk_div[i].val;
				return 0;
			}
		}
	}
	*pbclk = -1;
	*pmclk = -1;
	return -EINVAL;
}


static int sun4i_i2s_get_bclk_div(struct sun4i_i2s *i2s,
				  unsigned int oversample_rate,
				  unsigned int word_size)
{
	int div = oversample_rate / word_size / 2;
	int i;

	for (i = 0; i < ARRAY_SIZE(sun4i_i2s_bclk_div); i++) {
		const struct sun4i_i2s_clk_div *bdiv = &sun4i_i2s_bclk_div[i];

		if (bdiv->div == div)
			return bdiv->val;
	}

	return -EINVAL;
}

static int sun4i_i2s_get_mclk_div(struct sun4i_i2s *i2s,
				  unsigned int oversample_rate,
				  unsigned int module_rate,
				  unsigned int sampling_rate)
{
	int div = module_rate / sampling_rate / oversample_rate;
	int i;

	for (i = 0; i < ARRAY_SIZE(sun4i_i2s_mclk_div); i++) {
		const struct sun4i_i2s_clk_div *mdiv = &sun4i_i2s_mclk_div[i];

		if (mdiv->div == div)
			return mdiv->val;
	}

	return -EINVAL;
}

static int sun4i_i2s_oversample_rates[] = { 128, 192, 256, 384, 512, 768 };
static bool sun4i_i2s_oversample_is_valid(unsigned int oversample)
{
	int i;

	for (i = 0; i < ARRAY_SIZE(sun4i_i2s_oversample_rates); i++)
		if (sun4i_i2s_oversample_rates[i] == oversample)
			return true;

	return false;
}

static int sun4i_i2s_set_clk_rate(struct sun4i_i2s *i2s,
				  unsigned int rate,
				  unsigned int word_size)
{
	unsigned int oversample_rate, clk_rate;
	int bclk_div, mclk_div;
	int ret;

	switch (rate) {
	case 176400:
	case 88200:
	case 44100:
	case 22050:
	case 11025:
		clk_rate = 22579200;
		break;

	case 192000:
	case 128000:
	case 96000:
	case 64000:
	case 48000:
	case 32000:
	case 24000:
	case 16000:
	case 12000:
	case 8000:
		clk_rate = 24576000;
		break;

	default:
		return -EINVAL;
	}

	ret = clk_set_rate(i2s->mod_clk, clk_rate);
	if (ret)
		return ret;

	oversample_rate = i2s->mclk_freq / rate;
	if (!sun4i_i2s_oversample_is_valid(oversample_rate))
		return -EINVAL;

	bclk_div = sun4i_i2s_get_bclk_div(i2s, oversample_rate,
					  word_size);
	if (bclk_div < 0)
		return -EINVAL;

	mclk_div = sun4i_i2s_get_mclk_div(i2s, oversample_rate,
					  clk_rate, rate);
	if (mclk_div < 0)
		return -EINVAL;

	regmap_write(i2s->regmap, SUN4I_I2S_CLK_DIV_REG,
		     SUN4I_I2S_CLK_DIV_BCLK(bclk_div) |
		     SUN4I_I2S_CLK_DIV_MCLK(mclk_div) |
		     SUN4I_I2S_CLK_DIV_MCLK_EN);

	return 0;
}

static int sun4i_i2s_hw_params(struct snd_pcm_substream *substream,
			       struct snd_pcm_hw_params *params,
			       struct snd_soc_dai *dai)
{
	struct sun4i_i2s *i2s = snd_soc_dai_get_drvdata(dai);
	int sr, wss;
	u32 width, channels = 0;

	printk("%s COOPS\n", __func__);
	switch (params_channels(params)) {
	case 8:
		channels |= SUN4I_I2S_CTRL_SDO_EN(3);
	case 6:
		channels |= SUN4I_I2S_CTRL_SDO_EN(2);
	case 4:
		channels |= SUN4I_I2S_CTRL_SDO_EN(1);
	case 2:
		channels |= SUN4I_I2S_CTRL_SDO_EN(0);
		break;
	default:
		dev_err(dai->dev, "invalid channel: %d\n",
			params_channels(params));
		return -EINVAL;
	}

	regmap_update_bits(i2s->regmap, SUN4I_I2S_CTRL_REG,
			   SUN4I_I2S_CTRL_SDO_EN_MASK, channels);

	/* Enable the channels */
	regmap_write(i2s->regmap, SUN4I_I2S_TX_CHAN_SEL_REG,
		     SUN4I_I2S_TX_CHAN_SEL(params_channels(params)));

	switch (params_physical_width(params)) {
	case 16:
		width = DMA_SLAVE_BUSWIDTH_2_BYTES;
		break;
	case 24:
	case 32:
		width = DMA_SLAVE_BUSWIDTH_4_BYTES;
		break;
	default:
		return -EINVAL;
	}
	i2s->playback_dma_data.addr_width = width;

	switch (params_width(params)) {
	case 16:
		sr = 0;
		wss = 0;
		break;
	case 20:
		sr = 1;
		wss = 1;
		break;
	case 24:
		sr = 2;
		wss = 2;
		break;
	default:
		return -EINVAL;
	}

	regmap_update_bits(i2s->regmap, SUN4I_I2S_FMT0_REG,
			   SUN4I_I2S_FMT0_WSS_MASK | SUN4I_I2S_FMT0_SR_MASK,
			   SUN4I_I2S_FMT0_WSS(wss) | SUN4I_I2S_FMT0_SR(sr));
	{
	u32 reg_val = 0;
	regmap_read(i2s->regmap, SUN4I_I2S_FMT0_REG, &reg_val);
	printk("%s SUN4I_I2S_FMT0_REG 0x%x\n", __func__, reg_val);
	}
	return sun4i_i2s_set_clk_rate(i2s, params_rate(params),
				      params_physical_width(params));
}

static int sun4i_i2s_set_fmt(struct snd_soc_dai *dai, unsigned int fmt)
{
	struct sun4i_i2s *i2s = snd_soc_dai_get_drvdata(dai);
	u32 val;

	/* DAI Mode */
	switch (fmt & SND_SOC_DAIFMT_FORMAT_MASK) {
	case SND_SOC_DAIFMT_I2S:
		val = SUN4I_I2S_FMT0_FMT_I2S;
		break;
	case SND_SOC_DAIFMT_LEFT_J:
		val = SUN4I_I2S_FMT0_FMT_LEFT_J;
		break;
	case SND_SOC_DAIFMT_RIGHT_J:
		val = SUN4I_I2S_FMT0_FMT_RIGHT_J;
		break;
	default:
		return -EINVAL;
	}

	regmap_update_bits(i2s->regmap, SUN4I_I2S_FMT0_REG,
			   SUN4I_I2S_FMT0_FMT_MASK,
			   0 /*val*/);
	{
	u32 reg_val = 0;
	regmap_read(i2s->regmap, SUN4I_I2S_FMT0_REG, &reg_val);
	printk("%s 1 SUN4I_I2S_FMT0_REG 0x%x\n", __func__, reg_val);
	}

	/* DAI clock polarity */
	switch (fmt & SND_SOC_DAIFMT_INV_MASK) {
	case SND_SOC_DAIFMT_IB_IF:
		/* Invert both clocks */
		val = SUN4I_I2S_FMT0_BCLK_POLARITY_INVERTED |
			SUN4I_I2S_FMT0_LRCLK_POLARITY_INVERTED;
		break;
	case SND_SOC_DAIFMT_IB_NF:
		/* Invert bit clock */
		val = SUN4I_I2S_FMT0_BCLK_POLARITY_INVERTED |
			SUN4I_I2S_FMT0_LRCLK_POLARITY_NORMAL;
		break;
	case SND_SOC_DAIFMT_NB_IF:
		/* Invert frame clock */
		val = SUN4I_I2S_FMT0_LRCLK_POLARITY_INVERTED |
			SUN4I_I2S_FMT0_BCLK_POLARITY_NORMAL;
		break;
	case SND_SOC_DAIFMT_NB_NF:
		/* Nothing to do for both normal cases */
		val = SUN4I_I2S_FMT0_BCLK_POLARITY_NORMAL |
			SUN4I_I2S_FMT0_LRCLK_POLARITY_NORMAL;
		break;
	default:
		return -EINVAL;
	}

	regmap_update_bits(i2s->regmap, SUN4I_I2S_FMT0_REG,
			   SUN4I_I2S_FMT0_BCLK_POLARITY_MASK |
			   SUN4I_I2S_FMT0_LRCLK_POLARITY_MASK,
			   val);

	{
	u32 reg_val = 0;
	regmap_read(i2s->regmap, SUN4I_I2S_FMT0_REG, &reg_val);
	printk("%s 2 SUN4I_I2S_FMT0_REG 0x%x\n", __func__, reg_val);
	}
	/* DAI clock master masks */
	switch (fmt & SND_SOC_DAIFMT_MASTER_MASK) {
	case SND_SOC_DAIFMT_CBS_CFS:
		/* BCLK and LRCLK master */
		val = SUN4I_I2S_CTRL_MODE_MASTER;
		break;
	case SND_SOC_DAIFMT_CBM_CFM:
		/* BCLK and LRCLK slave */
		val = SUN4I_I2S_CTRL_MODE_SLAVE;
		break;
	default:
		return -EINVAL;
	}

	regmap_update_bits(i2s->regmap, SUN4I_I2S_CTRL_REG,
			   SUN4I_I2S_CTRL_MODE_MASK,
			   0 /*val*/);

	/* Set significant bits in our FIFOs */
	regmap_update_bits(i2s->regmap, SUN4I_I2S_FIFO_CTRL_REG,
			   SUN4I_I2S_FIFO_CTRL_TX_MODE_MASK |
			   SUN4I_I2S_FIFO_CTRL_RX_MODE_MASK,
			   SUN4I_I2S_FIFO_CTRL_TX_MODE(1) |
			   SUN4I_I2S_FIFO_CTRL_RX_MODE(1));
	return 0;
}

static int sun8i_i2s_set_clk_rate(struct sun4i_i2s *i2s,
				  unsigned int rate,
				  unsigned int word_size)
{
	unsigned int clk_rate;
	int bclk_div, mclk_div;
	int ret, i;

	switch (rate) {
	case 176400:
	case 88200:
	case 44100:
	case 22050:
	case 11025:
		clk_rate = 22579200;
		break;

	case 192000:
	case 128000:
	case 96000:
	case 64000:
	case 48000:
	case 32000:
	case 24000:
	case 16000:
	case 12000:
	case 8000:
		clk_rate = 24576000;
		break;

	default:
		return -EINVAL;
	}

	ret = clk_set_rate(i2s->mod_clk, clk_rate);
	if (ret)
		return ret;

	/* Always favor the highest oversampling rate */
	/*
	for (i = (ARRAY_SIZE(sun4i_i2s_oversample_rates) - 1); i >= 0; i--) {
		unsigned int oversample_rate = sun4i_i2s_oversample_rates[i];

		bclk_div = sun4i_i2s_get_bclk_div(i2s, oversample_rate,
						  word_size);
		mclk_div = sun4i_i2s_get_mclk_div(i2s, oversample_rate,
						  clk_rate,
						  rate);

		if ((bclk_div >= 0) && (mclk_div >= 0))
			break;
	}
	*/

	if ( 0 != sun8i_calc_bclk_mclk(rate, clk_rate,
				word_size, 2,
				&bclk_div, &mclk_div) ) {
		printk("calc bclk mclk error! rate = %d, pll2 = %d, wss = %d\n", rate, clk_rate, word_size);
		return -EINVAL;
	}

	if ((bclk_div < 0) || (mclk_div < 0))
		return -EINVAL;
	printk("SUN8I_I2S bclk is %d mclk is %d word size is %d\n", bclk_div, mclk_div, word_size);

	regmap_write(i2s->regmap, SUN4I_I2S_CLK_DIV_REG,
		     SUN4I_I2S_CLK_DIV_BCLK(bclk_div) |
		     SUN4I_I2S_CLK_DIV_MCLK(1) |
		     SUN8I_I2S_CLK_DIV_MCLK_EN);


	return 0;
}

static int sun8i_i2s_hw_params(struct snd_pcm_substream *substream,
			       struct snd_pcm_hw_params *params,
			       struct snd_soc_dai *dai)
{
	struct sun4i_i2s *i2s = snd_soc_dai_get_drvdata(dai);
	int sr, wss;
	u32 width, channels = 0;

	switch (params_channels(params)) {
	case 2:
		channels |= SUN4I_I2S_CTRL_SDO_EN(0);
		break;
	default:
		dev_err(dai->dev, "invalid channel: %d\n",
			params_channels(params));
		return -EINVAL;
	}
	regmap_update_bits(i2s->regmap, SUN4I_I2S_CTRL_REG,
			   SUN4I_I2S_CTRL_SDO_EN_MASK, channels);

	/* Configure the channels */
	regmap_write(i2s->regmap, SUN8I_I2S_TX_CHAN_CFG_REG,
		     SUN4I_I2S_TX_CHAN_SEL(params_channels(params)));

	/* Select the channels */
	regmap_update_bits(i2s->regmap, SUN8I_I2S_TX_CHAN_SEL_REG,
			   SUN8I_I2S_TX_CHAN_EN_MASK |
			   SUN8I_I2S_TX_CHAN_SEL_MASK,
			   SUN8I_I2S_TX_CHAN_EN(params_channels(params)) |
			   SUN8I_I2S_TX_CHAN_SEL(params_channels(params)));

	/* Map the channels This needs some work! */
	/*
	regmap_write(i2s->regmap, SUN8I_I2S_TX_CHAN_MAP_REG,
		     SUN4I_I2S_TX_CHAN_MAP(1,1));
	*/
	regmap_write(i2s->regmap, SUN8I_I2S_TX_CHAN_MAP_REG,
		     0x07654321);

	switch (params_physical_width(params)) {
	case 16:
		width = DMA_SLAVE_BUSWIDTH_2_BYTES;
		break;
	case 24:
	case 32:
		width = DMA_SLAVE_BUSWIDTH_4_BYTES;
		break;
	default:
		return -EINVAL;
	}
	i2s->playback_dma_data.addr_width = width;

	switch (params_width(params)) {
	case 16:
		sr = 3;
		break;
	case 20:
		sr = 4;
		break;
	case 24:
		sr = 5;
		break;
	case 32:
		sr = 7;
		break;
	default:
		return -EINVAL;
	}

	switch (params_physical_width(params)) {
	case 16:
		wss = 3;
		break;
	case 20:
		wss = 4;
		break;
	case 24:
		wss = 5;
		break;
	case 32:
		wss = 7;
		break;
	default:
		return -EINVAL;
	}

	regmap_update_bits(i2s->regmap, SUN4I_I2S_FMT0_REG,
			   SUN8I_I2S_FMT0_WSS_MASK | SUN8I_I2S_FMT0_SR_MASK,
			   SUN8I_I2S_FMT0_WSS(wss) | SUN4I_I2S_FMT0_SR(sr));

	regmap_update_bits(i2s->regmap, SUN4I_I2S_FMT0_REG,
		     SUN8I_I2S_FMT0_LRCK_PERIOD_MASK,
		     SUN8I_I2S_FMT0_LRCK_PERIOD(params_physical_width(params)-1));

	regmap_write(i2s->regmap, SUN4I_I2S_FMT1_REG, 0);

	return sun8i_i2s_set_clk_rate(i2s, params_rate(params),
				      params_physical_width(params));
}

static int sun8i_i2s_set_fmt(struct snd_soc_dai *dai, unsigned int fmt)
{
	struct sun4i_i2s *i2s = snd_soc_dai_get_drvdata(dai);
	u32 val = SUN8I_I2S_CTRL_MODE_I2S;
	u32 offset = 0;

	/* DAI Mode */
	switch (fmt & SND_SOC_DAIFMT_FORMAT_MASK) {
	case SND_SOC_DAIFMT_I2S:
		offset = 1;
		break;
	case SND_SOC_DAIFMT_LEFT_J:
		break;
	case SND_SOC_DAIFMT_RIGHT_J:
		val = SUN8I_I2S_CTRL_MODE_RIGHT_J;
		break;
	default:
		return -EINVAL;
	}

	regmap_update_bits(i2s->regmap, SUN4I_I2S_CTRL_REG,
			   SUN8I_I2S_CTRL_MODE_MASK,
			   val);

	/* blck offset determines whether i2s or LJ */
	regmap_update_bits(i2s->regmap, SUN8I_I2S_TX_CHAN_SEL_REG,
			   SUN8I_I2S_TX_CHAN_OFFSET_MASK,
			   SUN8I_I2S_TX_CHAN_OFFSET(offset));

	/* DAI clock polarity */
	switch (fmt & SND_SOC_DAIFMT_INV_MASK) {
	case SND_SOC_DAIFMT_IB_IF:
		/* Invert both clocks */
		val = SUN8I_I2S_FMT0_BCLK_POLARITY_INVERTED |
			SUN8I_I2S_FMT0_LRCLK_POLARITY_INVERTED;
		break;
	case SND_SOC_DAIFMT_IB_NF:
		/* Invert bit clock */
		val = SUN8I_I2S_FMT0_BCLK_POLARITY_INVERTED |
			SUN8I_I2S_FMT0_LRCLK_POLARITY_NORMAL;
		break;
	case SND_SOC_DAIFMT_NB_IF:
		/* Invert frame clock */
		val = SUN8I_I2S_FMT0_LRCLK_POLARITY_INVERTED |
			SUN8I_I2S_FMT0_BCLK_POLARITY_NORMAL;
		break;
	case SND_SOC_DAIFMT_NB_NF:
		/* Nothing to do for both normal cases */
		val = SUN8I_I2S_FMT0_BCLK_POLARITY_NORMAL |
			SUN8I_I2S_FMT0_LRCLK_POLARITY_NORMAL;
		break;
	default:
		return -EINVAL;
	}

	regmap_update_bits(i2s->regmap, SUN4I_I2S_FMT0_REG,
			   SUN8I_I2S_FMT0_BCLK_POLARITY_MASK |
			   SUN8I_I2S_FMT0_LRCLK_POLARITY_MASK,
			   val);

	/* Set significant bits in our FIFOs */
	regmap_update_bits(i2s->regmap, SUN4I_I2S_FIFO_CTRL_REG,
			   SUN4I_I2S_FIFO_CTRL_TX_MODE_MASK |
			   SUN4I_I2S_FIFO_CTRL_RX_MODE_MASK,
			   SUN4I_I2S_FIFO_CTRL_TX_MODE(1) |
			   SUN4I_I2S_FIFO_CTRL_RX_MODE(1));
	return 0;
}

static void sun4i_i2s_start_capture(struct sun4i_i2s *i2s)
{
	/* Flush RX FIFO */
	regmap_update_bits(i2s->regmap, SUN4I_I2S_FIFO_CTRL_REG,
			   SUN4I_I2S_FIFO_CTRL_FLUSH_RX,
			   SUN4I_I2S_FIFO_CTRL_FLUSH_RX);

	/* Clear RX counter */
	regmap_write(i2s->regmap, SUN4I_I2S_RX_CNT_REG, 0);

	/* Enable RX Block */
	regmap_update_bits(i2s->regmap, SUN4I_I2S_CTRL_REG,
			   SUN4I_I2S_CTRL_RX_EN,
			   SUN4I_I2S_CTRL_RX_EN);

	/* Enable RX DRQ */
	regmap_update_bits(i2s->regmap, SUN4I_I2S_DMA_INT_CTRL_REG,
			   SUN4I_I2S_DMA_INT_CTRL_RX_DRQ_EN,
			   SUN4I_I2S_DMA_INT_CTRL_RX_DRQ_EN);

	/* Debugging without codec */
	if(i2s->loopback)
		regmap_update_bits(i2s->regmap, SUN4I_I2S_CTRL_REG,
			   SUN4I_I2S_CTRL_LOOP,
			   SUN4I_I2S_CTRL_LOOP);
}

static void sun4i_i2s_start_playback(struct sun4i_i2s *i2s)
{
	/* Flush TX FIFO */
	regmap_update_bits(i2s->regmap, SUN4I_I2S_FIFO_CTRL_REG,
			   SUN4I_I2S_FIFO_CTRL_FLUSH_TX,
			   SUN4I_I2S_FIFO_CTRL_FLUSH_TX);

	/* Clear TX counter */
	regmap_write(i2s->regmap, SUN4I_I2S_TX_CNT_REG, 0);

	/* Enable TX Block */
	regmap_update_bits(i2s->regmap, SUN4I_I2S_CTRL_REG,
			   SUN4I_I2S_CTRL_TX_EN,
			   SUN4I_I2S_CTRL_TX_EN);

	/* Enable TX DRQ */
	regmap_update_bits(i2s->regmap, SUN4I_I2S_DMA_INT_CTRL_REG,
			   SUN4I_I2S_DMA_INT_CTRL_TX_DRQ_EN,
			   SUN4I_I2S_DMA_INT_CTRL_TX_DRQ_EN);
}

static void sun4i_i2s_stop_capture(struct sun4i_i2s *i2s)
{
	/* Disable RX Block */
	regmap_update_bits(i2s->regmap, SUN4I_I2S_CTRL_REG,
			   SUN4I_I2S_CTRL_RX_EN,
			   0);

	/* Disable RX DRQ */
	regmap_update_bits(i2s->regmap, SUN4I_I2S_DMA_INT_CTRL_REG,
			   SUN4I_I2S_DMA_INT_CTRL_RX_DRQ_EN,
			   0);
}

static void sun4i_i2s_stop_playback(struct sun4i_i2s *i2s)
{
	/* Disable TX Block */
	regmap_update_bits(i2s->regmap, SUN4I_I2S_CTRL_REG,
			   SUN4I_I2S_CTRL_TX_EN,
			   0);

	/* Disable TX DRQ */
	regmap_update_bits(i2s->regmap, SUN4I_I2S_DMA_INT_CTRL_REG,
			   SUN4I_I2S_DMA_INT_CTRL_TX_DRQ_EN,
			   0);
}

static int sun4i_i2s_trigger(struct snd_pcm_substream *substream, int cmd,
			     struct snd_soc_dai *dai)
{
	struct sun4i_i2s *i2s = snd_soc_dai_get_drvdata(dai);

	switch (cmd) {
	case SNDRV_PCM_TRIGGER_START:
	case SNDRV_PCM_TRIGGER_PAUSE_RELEASE:
	case SNDRV_PCM_TRIGGER_RESUME:
		if (substream->stream == SNDRV_PCM_STREAM_PLAYBACK)
			sun4i_i2s_start_playback(i2s);
		else
			sun4i_i2s_start_capture(i2s);
		break;

	case SNDRV_PCM_TRIGGER_STOP:
	case SNDRV_PCM_TRIGGER_PAUSE_PUSH:
	case SNDRV_PCM_TRIGGER_SUSPEND:
		if (substream->stream == SNDRV_PCM_STREAM_PLAYBACK)
			sun4i_i2s_stop_playback(i2s);
		else
			sun4i_i2s_stop_capture(i2s);
		break;

	default:
		return -EINVAL;
	}
	{
	/* COOPS DEBUGGING FOR NOW */
	/*
	u32 reg_val = 0;

	printk("I2S Command State %d Audio Clock is %lu\n", cmd, clk_get_rate(i2s->mod_clk));
	regmap_read(i2s->regmap, SUN4I_I2S_CTRL_REG, &reg_val);
	printk("SUN4I_I2S_CTRL_REG 0x%x\n", reg_val);
	regmap_read(i2s->regmap, SUN4I_I2S_FMT0_REG, &reg_val);
	printk("SUN4I_I2S_FMT0_REG 0x%x\n", reg_val);
	regmap_read(i2s->regmap, SUN4I_I2S_FMT1_REG, &reg_val);
	printk("SUN4I_I2S_FMT1_REG 0x%x\n", reg_val);
	regmap_read(i2s->regmap, SUN4I_I2S_FIFO_CTRL_REG, &reg_val);
	printk("SUN4I_I2S_FIFO_CTRL_REG 0x%x\n", reg_val);
	regmap_read(i2s->regmap, SUN4I_I2S_CLK_DIV_REG, &reg_val);
	printk("SUN4I_I2S_CLK_DIV_REG 0x%x\n", reg_val);
	regmap_read(i2s->regmap, SUN4I_I2S_FIFO_STA_REG, &reg_val);
	printk("SUN4I_I2S_FIFO_STA_REG 0x%x\n", reg_val);
	regmap_read(i2s->regmap, SUN4I_I2S_TX_CHAN_SEL_REG, &reg_val);
	printk("SUN4I_I2S_TX_CHAN_SEL_REG 0x%x\n", reg_val);
	regmap_read(i2s->regmap, SUN4I_I2S_TX_CHAN_MAP_REG, &reg_val);
	printk("SUN4I_I2S_TX_CHAN_MAP_REG 0x%x\n", reg_val);
	regmap_read(i2s->regmap, SUN8I_I2S_TX_CHAN_MAP_REG, &reg_val);
	printk("SUN8I_I2S_TX_CHAN_MAP_REG 0x%x\n", reg_val);
	*/
	}

	return 0;
}

static int sun4i_i2s_startup(struct snd_pcm_substream *substream,
			     struct snd_soc_dai *dai)
{
	struct sun4i_i2s *i2s = snd_soc_dai_get_drvdata(dai);

	/* Enable the whole hardware block */
	regmap_update_bits(i2s->regmap, SUN4I_I2S_CTRL_REG,
			   SUN4I_I2S_CTRL_GL_EN,SUN4I_I2S_CTRL_GL_EN);

	return clk_prepare_enable(i2s->mod_clk);
}

static void sun4i_i2s_shutdown(struct snd_pcm_substream *substream,
			       struct snd_soc_dai *dai)
{
	struct sun4i_i2s *i2s = snd_soc_dai_get_drvdata(dai);

	clk_disable_unprepare(i2s->mod_clk);

	/* Disable our output lines */
	regmap_update_bits(i2s->regmap, SUN4I_I2S_CTRL_REG,
			   SUN4I_I2S_CTRL_SDO_EN_MASK, 0);

	/* Disable the whole hardware block */
	regmap_update_bits(i2s->regmap, SUN4I_I2S_CTRL_REG, SUN4I_I2S_CTRL_GL_EN, 0);
}

static int sun4i_i2s_set_sysclk(struct snd_soc_dai *dai, int clk_id,
				unsigned int freq, int dir)
{
	struct sun4i_i2s *i2s = snd_soc_dai_get_drvdata(dai);

	if (clk_id != 0)
		return -EINVAL;

	i2s->mclk_freq = freq;

	return 0;
}

static const struct snd_soc_dai_ops sun4i_i2s_dai_ops = {
	.hw_params	= sun4i_i2s_hw_params,
	.set_fmt	= sun4i_i2s_set_fmt,
	.set_sysclk	= sun4i_i2s_set_sysclk,
	.shutdown	= sun4i_i2s_shutdown,
	.startup	= sun4i_i2s_startup,
	.trigger	= sun4i_i2s_trigger,
};

static const struct snd_soc_dai_ops sun8i_i2s_dai_ops = {
	.hw_params	= sun8i_i2s_hw_params,
	.set_fmt	= sun8i_i2s_set_fmt,
	.set_sysclk	= sun4i_i2s_set_sysclk,
	.shutdown	= sun4i_i2s_shutdown,
	.startup	= sun4i_i2s_startup,
	.trigger	= sun4i_i2s_trigger,
};

static int sun4i_i2s_dai_probe(struct snd_soc_dai *dai)
{
	struct sun4i_i2s *i2s = snd_soc_dai_get_drvdata(dai);

	snd_soc_dai_init_dma_data(dai,
				  &i2s->playback_dma_data,
				  &i2s->capture_dma_data);

	snd_soc_dai_set_drvdata(dai, i2s);

	return 0;
}

#define SUN4I_FORMATS	(SNDRV_PCM_FMTBIT_S16_LE | \
			 SNDRV_PCM_FMTBIT_S24_LE | \
			 SNDRV_PCM_FMTBIT_S32_LE)

static struct snd_soc_dai_driver sun4i_i2s_dai = {
	.probe = sun4i_i2s_dai_probe,
	.capture = {
		.stream_name = "Capture",
		.channels_min = 2,
		.channels_max = 2,
		.rates = SNDRV_PCM_RATE_8000_192000,
		.formats = SNDRV_PCM_FMTBIT_S16_LE,
	},
	.playback = {
		.stream_name = "Playback",
		.channels_min = 2,
		.channels_max = 2,
		.rates = SNDRV_PCM_RATE_8000_192000,
		.formats = SUN4I_FORMATS,
	},
	.symmetric_rates = 1,
};

static const struct snd_soc_component_driver sun4i_i2s_component = {
	.name	= "sun4i-dai",
};

static bool sun4i_i2s_rd_reg(struct device *dev, unsigned int reg)
{
	switch (reg) {
	case SUN4I_I2S_FIFO_TX_REG:
		return false;

	default:
		return true;
	}
}


static bool sun4i_i2s_wr_reg(struct device *dev, unsigned int reg)
{
	switch (reg) {
	case SUN4I_I2S_FIFO_RX_REG:
		return false;

	default:
		return true;
	}
}

static bool sun4i_i2s_volatile_reg(struct device *dev, unsigned int reg)
{
	switch (reg) {
	case SUN4I_I2S_FIFO_RX_REG:
	case SUN4I_I2S_FIFO_STA_REG:
	case SUN4I_I2S_INT_STA_REG:
	case SUN4I_I2S_RX_CNT_REG:
	case SUN4I_I2S_TX_CNT_REG:
		return true;

	default:
		return false;
	}
}

static bool sun8i_i2s_rd_reg(struct device *dev, unsigned int reg)
{
	switch (reg) {
	case SUN8I_I2S_FIFO_TX_REG:
		return false;

	default:
		return true;
	}
}

static bool sun8i_i2s_volatile_reg(struct device *dev, unsigned int reg)
{
	switch (reg) {
	case SUN4I_I2S_FIFO_RX_REG:
	case SUN4I_I2S_FIFO_CTRL_REG:
	case SUN4I_I2S_FIFO_STA_REG:
	case SUN8I_I2S_INT_STA_REG:
	case SUN4I_I2S_RX_CNT_REG:
	case SUN4I_I2S_TX_CNT_REG:
		return true;

	default:
		return false;
	}
}

static const struct reg_default sun4i_i2s_reg_defaults[] = {
	{ SUN4I_I2S_CTRL_REG, 0x00000000 },
	{ SUN4I_I2S_FMT0_REG, 0x0000000c },
	{ SUN4I_I2S_FMT1_REG, 0x00004020 },
	{ SUN4I_I2S_FIFO_CTRL_REG, 0x000400f0 },
	{ SUN4I_I2S_DMA_INT_CTRL_REG, 0x00000000 },
	{ SUN4I_I2S_CLK_DIV_REG, 0x00000000 },
	{ SUN4I_I2S_TX_CHAN_SEL_REG, 0x00000001 },
	{ SUN4I_I2S_TX_CHAN_MAP_REG, 0x76543210 },
	{ SUN4I_I2S_RX_CHAN_SEL_REG, 0x00000001 },
	{ SUN4I_I2S_RX_CHAN_MAP_REG, 0x00003210 },
};

static const struct reg_default sun8i_i2s_reg_defaults[] = {
	{ SUN4I_I2S_CTRL_REG, 0x00060000 },
	{ SUN4I_I2S_FMT0_REG, 0x00000033 },
	{ SUN4I_I2S_FMT1_REG, 0x00000030 },
	{ SUN4I_I2S_FIFO_CTRL_REG, 0x000400f0 },
	{ SUN4I_I2S_DMA_INT_CTRL_REG, 0x00000000 },
	{ SUN4I_I2S_CLK_DIV_REG, 0x00000000 },
	{ SUN8I_I2S_TX_CHAN_CFG_REG, 0x00000000 },
	{ SUN8I_I2S_TX_CHAN_SEL_REG, 0x00000000 },
	{ SUN4I_I2S_RX_CHAN_SEL_REG, 0x00000000 },
	{ SUN4I_I2S_RX_CHAN_MAP_REG, 0x00000000 },
	{ SUN8I_I2S_TX_CHAN_MAP_REG, 0x00000000 },
};

static const struct regmap_config sun4i_i2s_regmap_config = {
	.reg_bits	= 32,
	.reg_stride	= 4,
	.val_bits	= 32,
	.max_register	= SUN4I_I2S_RX_CHAN_MAP_REG,

	.cache_type	= REGCACHE_FLAT,
	.reg_defaults	= sun4i_i2s_reg_defaults,
	.num_reg_defaults	= ARRAY_SIZE(sun4i_i2s_reg_defaults),
	.writeable_reg	= sun4i_i2s_wr_reg,
	.readable_reg	= sun4i_i2s_rd_reg,
	.volatile_reg	= sun4i_i2s_volatile_reg,
};

static const struct regmap_config sun8i_i2s_regmap_config = {
	.reg_bits	= 32,
	.reg_stride	= 4,
	.val_bits	= 32,
	.max_register	= SUN8I_I2S_RX_CHAN_MAP_REG,
	.cache_type	= REGCACHE_FLAT,
	.reg_defaults	= sun8i_i2s_reg_defaults,
	.num_reg_defaults	= ARRAY_SIZE(sun8i_i2s_reg_defaults),
	.writeable_reg	= sun4i_i2s_wr_reg,
	.readable_reg	= sun8i_i2s_rd_reg,
	.volatile_reg	= sun8i_i2s_volatile_reg,
};

static int sun4i_i2s_runtime_resume(struct device *dev)
{
	struct sun4i_i2s *i2s = dev_get_drvdata(dev);
	int ret;

	ret = clk_prepare_enable(i2s->bus_clk);
	if (ret) {
		dev_err(dev, "Failed to enable bus clock\n");
		return ret;
	}

	regcache_cache_only(i2s->regmap, false);
	regcache_mark_dirty(i2s->regmap);

	ret = regcache_sync(i2s->regmap);
	if (ret) {
		dev_err(dev, "Failed to sync regmap cache\n");
		goto err_disable_clk;
	}

	return 0;

err_disable_clk:
	clk_disable_unprepare(i2s->bus_clk);
	return ret;
}

static int sun4i_i2s_runtime_suspend(struct device *dev)
{
	struct sun4i_i2s *i2s = dev_get_drvdata(dev);

	regcache_cache_only(i2s->regmap, true);

	clk_disable_unprepare(i2s->bus_clk);

	return 0;
}

struct sun4i_i2s_quirks {
	bool 		has_reset;
	unsigned int	reg_dac_txdata;	/* TX FIFO offset for DMA config */
	const struct regmap_config	*sun4i_i2s_regmap;
	const struct snd_soc_dai_ops	*ops;
};

static const struct sun4i_i2s_quirks sun4i_a10_i2s_quirks = {
	.has_reset		= false,
	.reg_dac_txdata		= SUN4I_I2S_FIFO_TX_REG,
	.sun4i_i2s_regmap	= &sun4i_i2s_regmap_config,
	.ops			= &sun4i_i2s_dai_ops,
};

static const struct sun4i_i2s_quirks sun6i_a31_i2s_quirks = {
	.has_reset		= true,
	.reg_dac_txdata		= SUN4I_I2S_FIFO_TX_REG,
	.sun4i_i2s_regmap	= &sun4i_i2s_regmap_config,
	.ops			= &sun4i_i2s_dai_ops,
};

static const struct sun4i_i2s_quirks sun8i_h3_i2s_quirks = {
	.has_reset		= true,
	.reg_dac_txdata		= SUN8I_I2S_FIFO_TX_REG,
	.sun4i_i2s_regmap 	= &sun8i_i2s_regmap_config,
	.ops			= &sun8i_i2s_dai_ops,
};

static const struct sun4i_i2s_quirks sun50i_a64_i2s_quirks = {
	.has_reset		= true,
	.reg_dac_txdata		= SUN8I_I2S_FIFO_TX_REG,
	.sun4i_i2s_regmap	= &sun4i_i2s_regmap_config,
	.ops			= &sun4i_i2s_dai_ops,
};

static int sun4i_i2s_probe(struct platform_device *pdev)
{
	struct sun4i_i2s *i2s;
	const struct sun4i_i2s_quirks *quirks;
	struct resource *res;
	void __iomem *regs;
	int irq, ret;
	u32 val;

	i2s = devm_kzalloc(&pdev->dev, sizeof(*i2s), GFP_KERNEL);
	if (!i2s)
		return -ENOMEM;
	platform_set_drvdata(pdev, i2s);

	res = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	regs = devm_ioremap_resource(&pdev->dev, res);
	if (IS_ERR(regs))
		return PTR_ERR(regs);

	irq = platform_get_irq(pdev, 0);
	if (irq < 0) {
		dev_err(&pdev->dev, "Can't retrieve our interrupt\n");
		return irq;
	}

	quirks = of_device_get_match_data(&pdev->dev);
	if (!quirks) {
		dev_err(&pdev->dev, "Failed to determine the quirks to use\n");
		return -ENODEV;
	}

	i2s->bus_clk = devm_clk_get(&pdev->dev, "apb");
	if (IS_ERR(i2s->bus_clk)) {
		dev_err(&pdev->dev, "Can't get our bus clock\n");
		return PTR_ERR(i2s->bus_clk);
	}

	quirks = of_device_get_match_data(&pdev->dev);
	if (quirks == NULL) {
		dev_err(&pdev->dev, "Failed to determine the quirks to use\n");
		return -ENODEV;
	}

	i2s->regmap = devm_regmap_init_mmio(&pdev->dev, regs,
					    quirks->sun4i_i2s_regmap);
	if (IS_ERR(i2s->regmap)) {
		dev_err(&pdev->dev, "Regmap initialisation failed\n");
		return PTR_ERR(i2s->regmap);
	}

	i2s->mod_clk = devm_clk_get(&pdev->dev, "mod");
	if (IS_ERR(i2s->mod_clk)) {
		dev_err(&pdev->dev, "Can't get our mod clock\n");
		return PTR_ERR(i2s->mod_clk);
	}

	if (quirks->has_reset) {
		i2s->rst = devm_reset_control_get(&pdev->dev, NULL);
		if (IS_ERR(i2s->rst)) {
			dev_err(&pdev->dev, "Failed to get reset control\n");
			return PTR_ERR(i2s->rst);
		}
	}

	if (!IS_ERR(i2s->rst)) {
		ret = reset_control_deassert(i2s->rst);
		if (ret) {
			dev_err(&pdev->dev,
				"Failed to deassert the reset control\n");
			return -EINVAL;
		}
	}

	i2s->playback_dma_data.addr = res->start + quirks->reg_dac_txdata;
	i2s->playback_dma_data.maxburst = 8;

	i2s->capture_dma_data.addr = res->start + SUN4I_I2S_FIFO_RX_REG;
	i2s->capture_dma_data.maxburst = 8;


	if (of_property_read_bool(pdev->dev.of_node, "loopback"))
		i2s->loopback = true;

	pm_runtime_enable(&pdev->dev);
	if (!pm_runtime_enabled(&pdev->dev)) {
		ret = sun4i_i2s_runtime_resume(&pdev->dev);
		if (ret)
			goto err_pm_disable;
	}

	if (!of_property_read_u32(pdev->dev.of_node,
				  "allwinner,playback-channels", &val)) {
		if (val >= 2 && val <= 8)
			sun4i_i2s_dai.playback.channels_max = val;
	}

	/* Register ops with dai */
	sun4i_i2s_dai.ops = quirks->ops;
	ret = devm_snd_soc_register_component(&pdev->dev,
					      &sun4i_i2s_component,
					      &sun4i_i2s_dai, 1);
	if (ret) {
		dev_err(&pdev->dev, "Could not register DAI\n");
		goto err_suspend;
	}

	ret = snd_dmaengine_pcm_register(&pdev->dev, NULL, 0);
	if (ret) {
		dev_err(&pdev->dev, "Could not register PCM\n");
		goto err_suspend;
	}

	return 0;

err_suspend:
	if (!pm_runtime_status_suspended(&pdev->dev))
		sun4i_i2s_runtime_suspend(&pdev->dev);
err_pm_disable:
	pm_runtime_disable(&pdev->dev);
	if (!IS_ERR(i2s->rst))
		reset_control_assert(i2s->rst);

	return ret;
}

static int sun4i_i2s_remove(struct platform_device *pdev)
{
	struct sun4i_i2s *i2s = dev_get_drvdata(&pdev->dev);

	snd_dmaengine_pcm_unregister(&pdev->dev);

	pm_runtime_disable(&pdev->dev);
	if (!pm_runtime_status_suspended(&pdev->dev))
		sun4i_i2s_runtime_suspend(&pdev->dev);

	if (!IS_ERR(i2s->rst))
		reset_control_assert(i2s->rst);

	return 0;
}

static const struct of_device_id sun4i_i2s_match[] = {
	{
		.compatible = "allwinner,sun4i-a10-i2s",
		.data = &sun4i_a10_i2s_quirks,
	},
	{
		.compatible = "allwinner,sun6i-a31-i2s",
		.data = &sun6i_a31_i2s_quirks,
	},
	{
		.compatible = "allwinner,sun8i-h3-i2s",
		.data = &sun8i_h3_i2s_quirks,
	},
	{
		.compatible = "allwinner,sun50i-A64-i2s",
		.data = &sun50i_a64_i2s_quirks,
	},
	{} /* Sentinal */
};
MODULE_DEVICE_TABLE(of, sun4i_i2s_match);

static const struct dev_pm_ops sun4i_i2s_pm_ops = {
	.runtime_resume		= sun4i_i2s_runtime_resume,
	.runtime_suspend	= sun4i_i2s_runtime_suspend,
};

static struct platform_driver sun4i_i2s_driver = {
	.probe	= sun4i_i2s_probe,
	.remove	= sun4i_i2s_remove,
	.driver	= {
		.name		= "sun4i-i2s",
		.of_match_table	= sun4i_i2s_match,
		.pm		= &sun4i_i2s_pm_ops,
	},
};
module_platform_driver(sun4i_i2s_driver);

MODULE_AUTHOR("Andrea Venturi <be17068@iperbole.bo.it>");
MODULE_AUTHOR("Maxime Ripard <maxime.ripard@free-electrons.com>");
MODULE_DESCRIPTION("Allwinner A10 I2S driver");
MODULE_LICENSE("GPL");
