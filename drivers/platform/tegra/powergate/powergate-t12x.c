/*
 * Copyright (c) 2013-2015, NVIDIA CORPORATION.  All rights reserved.
 *
 * This software is licensed under the terms of the GNU General Public
 * License version 2, as published by the Free Software Foundation, and
 * may be copied, distributed, and modified under those terms.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 */

#include <linux/spinlock.h>
#include <linux/delay.h>
#include <linux/regulator/consumer.h>
#include <linux/tegra-powergate.h>

#include <asm/atomic.h>

#include "powergate-priv.h"
#include "powergate-ops-t1xx.h"
#include <linux/platform/tegra/dvfs.h>
#include <linux/platform/tegra/mc.h>
#include <linux/tegra_soctherm.h>
#include <linux/tegra-fuse.h>

enum mc_client_type {
	MC_CLIENT_AFI		= 0,
	MC_CLIENT_DC		= 2,
	MC_CLIENT_DCB		= 3,
	MC_CLIENT_ISP		= 8,
	MC_CLIENT_MSENC		= 11,
	MC_CLIENT_SATA		= 15,
	MC_CLIENT_VDE		= 16,
	MC_CLIENT_VI		= 17,
	MC_CLIENT_VIC		= 18,
	MC_CLIENT_XUSB_HOST	= 19,
	MC_CLIENT_XUSB_DEV	= 20,
	MC_CLIENT_ISPB		= 33,
	MC_CLIENT_GPU		= 34,
	MC_CLIENT_LAST		= -1,
};

struct tegra12x_powergate_mc_client_info {
	enum mc_client_type hot_reset_clients[MAX_HOTRESET_CLIENT_NUM];
};

static struct tegra12x_powergate_mc_client_info tegra12x_pg_mc_info[] = {
	[TEGRA_POWERGATE_CRAIL] = {
		.hot_reset_clients = {
			[0] = MC_CLIENT_LAST,
		},
	},
	[TEGRA_POWERGATE_GPU] = {
		.hot_reset_clients = {
			[0] = MC_CLIENT_GPU,
			[1] = MC_CLIENT_LAST,
		},
	},
	[TEGRA_POWERGATE_VDEC] = {
		.hot_reset_clients = {
			[0] = MC_CLIENT_VDE,
			[1] = MC_CLIENT_LAST,
		},
	},
	[TEGRA_POWERGATE_MPE] = {
		.hot_reset_clients = {
			[0] = MC_CLIENT_MSENC,
			[1] = MC_CLIENT_LAST,
		},
	},
	[TEGRA_POWERGATE_VENC] = {
		.hot_reset_clients = {
			[0] = MC_CLIENT_ISP,
			[1] = MC_CLIENT_ISPB,
			[2] = MC_CLIENT_VI,
			[3] = MC_CLIENT_LAST,
		},
	},
	[TEGRA_POWERGATE_CPU1] = {
		.hot_reset_clients = {
			[0] = MC_CLIENT_LAST,
		},
	},
	[TEGRA_POWERGATE_CPU2] = {
		.hot_reset_clients = {
			[0] = MC_CLIENT_LAST,
		},
	},
	[TEGRA_POWERGATE_CPU3] = {
		.hot_reset_clients = {
			[0] = MC_CLIENT_LAST,
		},
	},
	[TEGRA_POWERGATE_CELP] = {
		.hot_reset_clients = {
			[0] = MC_CLIENT_LAST,
		},
	},
	[TEGRA_POWERGATE_CPU0] = {
		.hot_reset_clients = {
			[0] = MC_CLIENT_LAST,
		},
	},
	[TEGRA_POWERGATE_C0NC] = {
		.hot_reset_clients = {
			[0] = MC_CLIENT_LAST,
		},
	},
	[TEGRA_POWERGATE_C1NC] = {
		.hot_reset_clients = {
			[0] = MC_CLIENT_LAST,
		},
	},
	[TEGRA_POWERGATE_DISA] = {
		.hot_reset_clients = {
			[0] = MC_CLIENT_DC,
			[1] = MC_CLIENT_LAST,
		},
	},
	[TEGRA_POWERGATE_DISB] = {
		.hot_reset_clients = {
			[0] = MC_CLIENT_DCB,
			[1] = MC_CLIENT_LAST,
		},
	},
	[TEGRA_POWERGATE_XUSBA] = {
		.hot_reset_clients = {
			[0] = MC_CLIENT_LAST,
		},
	},
	[TEGRA_POWERGATE_XUSBB] = {
		.hot_reset_clients = {
			[0] = MC_CLIENT_XUSB_DEV,
			[1] = MC_CLIENT_LAST
		},
	},
	[TEGRA_POWERGATE_XUSBC] = {
		.hot_reset_clients = {
			[0] = MC_CLIENT_XUSB_HOST,
			[1] = MC_CLIENT_LAST,
		},
	},
#ifdef CONFIG_ARCH_TEGRA_HAS_PCIE
	[TEGRA_POWERGATE_PCIE] = {
		.hot_reset_clients = {
			[0] = MC_CLIENT_AFI,
			[1] = MC_CLIENT_LAST,
		},
	},
#endif
#ifdef CONFIG_ARCH_TEGRA_HAS_SATA
	[TEGRA_POWERGATE_SATA] = {
		.hot_reset_clients = {
			[0] = MC_CLIENT_SATA,
			[1] = MC_CLIENT_LAST,
		},
	},
#endif
	[TEGRA_POWERGATE_SOR] = {
		.hot_reset_clients = {
			[0] = MC_CLIENT_LAST,
		},
	},
#ifdef CONFIG_ARCH_TEGRA_VIC
	[TEGRA_POWERGATE_VIC] = {
		.hot_reset_clients = {
			[0] = MC_CLIENT_VIC,
			[1] = MC_CLIENT_LAST,
		},
	},
#endif
};

static struct powergate_partition_info tegra12x_powergate_partition_info[] = {
	[TEGRA_POWERGATE_CRAIL] = { .name = "crail" },
	[TEGRA_POWERGATE_GPU] = {
		.name = "gpu",
		.clk_info = {
			[0] = { .clk_name = "gpu_ref", .clk_type = CLK_AND_RST },
			[1] = { .clk_name = "pll_p_out5", .clk_type = CLK_ONLY },
		},
	},
	[TEGRA_POWERGATE_VDEC] = {
		.name = "vde",
		.clk_info = {
			[0] = { .clk_name = "vde", .clk_type = CLK_AND_RST },
		},
	},
	[TEGRA_POWERGATE_MPE] = {
		.name = "mpe",
		.clk_info = {
			[0] = { .clk_name = "msenc.cbus", .clk_type = CLK_AND_RST },
		},
	},
	[TEGRA_POWERGATE_VENC] = {
		.name = "ve",
		.clk_info = {
			[0] = { .clk_name = "ispa", .clk_type = CLK_AND_RST },
			[1] = { .clk_name = "ispb", .clk_type = CLK_AND_RST },
			[2] = { .clk_name = "vi", .clk_type = CLK_AND_RST },
			[3] = { .clk_name = "csi", .clk_type = CLK_AND_RST },
		},
	},
	[TEGRA_POWERGATE_CPU1] = { .name = "cpu1" },
	[TEGRA_POWERGATE_CPU2] = { .name = "cpu2" },
	[TEGRA_POWERGATE_CPU3] = { .name = "cpu3" },
	[TEGRA_POWERGATE_CELP] = { .name = "celp" },
	[TEGRA_POWERGATE_CPU0] = { .name = "cpu0" },
	[TEGRA_POWERGATE_C0NC] = { .name = "c0nc" },
	[TEGRA_POWERGATE_C1NC] = { .name = "c1nc" },
	[TEGRA_POWERGATE_DISA] = {
		.name = "disa",
		.clk_info = {
			[0] = { .clk_name = "disp1", .clk_type = CLK_AND_RST },
		},
	},
	[TEGRA_POWERGATE_DISB] = {
		.name = "disb",
		.clk_info = {
			[0] = { .clk_name = "disp2", .clk_type = CLK_AND_RST },
		},
	},
	[TEGRA_POWERGATE_XUSBA] = {
		.name = "xusba",
		.clk_info = {
			[0] = { .clk_name = "xusb_ss", .clk_type = CLK_AND_RST },
		},
	},
	[TEGRA_POWERGATE_XUSBB] = {
		.name = "xusbb",
		.clk_info = {
			[0] = { .clk_name = "xusb_dev", .clk_type = CLK_AND_RST },
		},
	},
	[TEGRA_POWERGATE_XUSBC] = {
		.name = "xusbc",
		.clk_info = {
			[0] = { .clk_name = "xusb_host", .clk_type = CLK_AND_RST },
		},
	},
#ifdef CONFIG_ARCH_TEGRA_HAS_PCIE
	[TEGRA_POWERGATE_PCIE] = {
		.name = "pcie",
		.clk_info = {
			[0] = { .clk_name = "afi", .clk_type = CLK_AND_RST },
			[1] = { .clk_name = "pcie", .clk_type = CLK_AND_RST },
			[2] = { .clk_name = "pciex", .clk_type = RST_ONLY },
		},
		.skip_reset = true,
	},
#endif
#ifdef CONFIG_ARCH_TEGRA_HAS_SATA
	[TEGRA_POWERGATE_SATA] = {
		.name = "sata",
		.clk_info = {
			[0] = { .clk_name = "sata", .clk_type = CLK_AND_RST },
			[1] = { .clk_name = "sata_oob", .clk_type = CLK_AND_RST },
			[2] = { .clk_name = "cml1", .clk_type = CLK_ONLY },
			[3] = { .clk_name = "sata_cold", .clk_type = RST_ONLY },
		},
	},
#endif
	[TEGRA_POWERGATE_SOR] = {
		.name = "sor",
		.clk_info = {
			[0] = { .clk_name = "sor0", .clk_type = CLK_AND_RST },
			[1] = { .clk_name = "dsia", .clk_type = CLK_AND_RST },
			[2] = { .clk_name = "dsib", .clk_type = CLK_AND_RST },
			[3] = { .clk_name = "hdmi", .clk_type = CLK_AND_RST },
			[4] = { .clk_name = "mipi-cal", .clk_type = CLK_AND_RST },
			[5] = { .clk_name = "dpaux", .clk_type = CLK_ONLY },
		},
	},
#ifdef CONFIG_ARCH_TEGRA_VIC
	[TEGRA_POWERGATE_VIC] = {
		.name = "vic",
		.clk_info = {
			[0] = { .clk_name = "vic03.cbus", .clk_type = CLK_AND_RST },
		},
	},
#endif
};

#define MC_CLIENT_HOTRESET_CTRL		0x200
#define MC_CLIENT_HOTRESET_STAT		0x204
#define MC_CLIENT_HOTRESET_CTRL_1	0x970
#define MC_CLIENT_HOTRESET_STAT_1	0x974
#define MC_VIDEO_PROTECT_REG_CTRL	0x650

#define PMC_GPU_RG_CNTRL_0		0x2d4

#define UTMIPLL_HW_PWRDN_CFG0		0x52c
#define UTMIPLL_HW_PWRDN_CFG0_IDDQ_OVERRIDE  (1<<1)
#define UTMIPLL_HW_PWRDN_CFG0_IDDQ_SWCTL     (1<<0)

static DEFINE_SPINLOCK(tegra12x_powergate_lock);
static DEFINE_MUTEX(tegra12x_powergate_disp_lock);

static struct dvfs_rail *gpu_rail;

#define HOTRESET_READ_COUNT	5
int tegra12x_powergate_mc_enable(int id)
{
	return 0;
}

int tegra12x_powergate_mc_disable(int id)
{
	return 0;
}

int tegra12x_powergate_mc_flush(int id)
{
        u32 idx;
        enum mc_client_type mc_client_bit;
        int ret = -EINVAL;

	for (idx = 0; idx < MAX_HOTRESET_CLIENT_NUM; idx++) {
		mc_client_bit =
			tegra12x_pg_mc_info[id].hot_reset_clients[idx];
		if (mc_client_bit == MC_CLIENT_LAST)
			break;

               ret = tegra_mc_flush(mc_client_bit);
	}

	return ret;
}

int tegra12x_powergate_mc_flush_done(int id)
{
        u32 idx;
        enum mc_client_type mc_client_bit;
        int ret = -EINVAL;

	for (idx = 0; idx < MAX_HOTRESET_CLIENT_NUM; idx++) {
		mc_client_bit =
			tegra12x_pg_mc_info[id].hot_reset_clients[idx];
		if (mc_client_bit == MC_CLIENT_LAST)
			break;

                ret = tegra_mc_flush_done(mc_client_bit);
	}

	wmb();

	return ret;
}

static int tegra12x_gpu_powergate(int id, struct powergate_partition_info *pg_info)
{
	int ret;

	/* If first clk_ptr is null, fill clk info for the partition */
	if (!pg_info->clk_info[0].clk_ptr)
		get_clk_info(pg_info);

	tegra_powergate_mc_flush(id);

	udelay(10);

	/* enable clamp */
	pmc_write(0x1, PMC_GPU_RG_CNTRL_0);
	pmc_read(PMC_GPU_RG_CNTRL_0);

	udelay(10);

	powergate_partition_assert_reset(pg_info);

	udelay(10);

	/*
	 * GPCPLL is already disabled before entering this function; reference
	 * clocks are enabled until now - disable them just before rail gating
	 */
	partition_clk_disable(pg_info);

	udelay(10);

	tegra_soctherm_gpu_tsens_invalidate(1);

	if (gpu_rail && tegra_powergate_is_powered(id)) {
		ret = tegra_dvfs_rail_power_down(gpu_rail);
		if (ret)
			goto err_power_off;
	} else
		pr_info("No GPU regulator?\n");

	return 0;

err_power_off:
	WARN(1, "Could not Railgate Partition %d", id);
	return ret;
}

static int mc_check_vpr(void)
{
	int ret = 0;
	u32 val = mc_readl(MC_VIDEO_PROTECT_REG_CTRL);
	if ((val & 1) == 0) {
		pr_err("VPR configuration not locked down\n");
		ret = -EINVAL;
	}
	return ret;
}

static int tegra12x_gpu_unpowergate(int id,
	struct powergate_partition_info *pg_info)
{
	int ret = 0;
	bool first = false;

	ret = mc_check_vpr();
	if (ret)
		return ret;

	if (!gpu_rail) {
		gpu_rail = tegra_dvfs_get_rail_by_name("vdd_gpu");
		if (IS_ERR_OR_NULL(gpu_rail)) {
			WARN(1, "No GPU regulator?\n");
			goto err_power;
		}
		first = true;
	}

	ret = tegra_dvfs_rail_power_up(gpu_rail);
	if (ret)
		goto err_power;

	tegra_soctherm_gpu_tsens_invalidate(0);

	/* If first clk_ptr is null, fill clk info for the partition */
	if (!pg_info->clk_info[0].clk_ptr)
		get_clk_info(pg_info);

	/*
	 * GPU reference clocks are initially enabled - skip clock enable if
	 * 1st unpowergate, and in any case leave reference clock enabled on
	 * exit. GPCPLL is still disabled, and will be enabled by driver.
	 */
	if (!first) {
		/* Un-Powergating fails if all clks are not enabled */
		ret = partition_clk_enable(pg_info);
		if (ret)
			goto err_clk_on;
	}

	udelay(10);

	powergate_partition_assert_reset(pg_info);

	udelay(10);

	/* disable clamp */
	pmc_write(0, PMC_GPU_RG_CNTRL_0);
	pmc_read(PMC_GPU_RG_CNTRL_0);

	udelay(10);

	powergate_partition_deassert_reset(pg_info);

	udelay(10);

	/* Flush MC 1st time after boot or rail-gate */
	tegra_powergate_mc_flush(id);

	udelay(10);

	tegra_powergate_mc_flush_done(id);

	udelay(10);

	return 0;

err_clk_on:
	powergate_module(id);
err_power:
	WARN(1, "Could not Un-Railgate %d", id);
	return ret;
}

static atomic_t ref_count_dispa = ATOMIC_INIT(0);
static atomic_t ref_count_dispb = ATOMIC_INIT(0);
static atomic_t ref_count_venc = ATOMIC_INIT(0);
static atomic_t ref_count_pcie = ATOMIC_INIT(0);

#define CHECK_RET(x)			\
	do {				\
		ret = (x);		\
		if (ret != 0)		\
			return ret;	\
	} while (0)


static inline int tegra12x_powergate(int id)
{
	if (tegra_powergate_is_powered(id))
		return tegra1xx_powergate(id,
			&tegra12x_powergate_partition_info[id]);
	return 0;
}

static inline int tegra12x_unpowergate(int id)
{
	if (!tegra_powergate_is_powered(id))
		return tegra1xx_unpowergate(id,
			&tegra12x_powergate_partition_info[id]);
	return 0;
}

static int tegra12x_disp_powergate(int id)
{
	int ret = 0;
	int ref_counta = 0;
	int ref_countb = 0;

	mutex_lock(&tegra12x_powergate_disp_lock);

	ref_counta = atomic_read(&ref_count_dispa);
	ref_countb = atomic_read(&ref_count_dispb);

	if (id == TEGRA_POWERGATE_DISA) {
		if (ref_counta > 0)
			ref_counta = atomic_dec_return(&ref_count_dispa);
		if ((ref_counta <= 0) &&
			tegra12x_powergate(TEGRA_POWERGATE_DISA)) {
			ret = -EBUSY;
			goto error_out;
		}
	} else if (id == TEGRA_POWERGATE_DISB) {
		if (ref_countb > 0)
			ref_countb = atomic_dec_return(&ref_count_dispb);
		if ((ref_countb <= 0) &&
			tegra12x_powergate(TEGRA_POWERGATE_DISB)) {
			ret = -EBUSY;
			goto error_out;
		}
	}

	if ((ref_counta <= 0) && (ref_countb <= 0)) {
		if (tegra12x_powergate(TEGRA_POWERGATE_SOR)) {
			ret = -EBUSY;
			goto error_out;
		}
	}

error_out:
	mutex_unlock(&tegra12x_powergate_disp_lock);
	return ret;
}

static int tegra12x_disp_unpowergate(int id)
{
	int ret = 0;

	mutex_lock(&tegra12x_powergate_disp_lock);
	/* always unpowergate SOR partition */
	if (tegra12x_unpowergate(TEGRA_POWERGATE_SOR)) {
		ret = -EBUSY;
		goto error_out;
	}

	if (id == TEGRA_POWERGATE_DISA)
		atomic_inc(&ref_count_dispa);
	else if (id == TEGRA_POWERGATE_DISB)
		atomic_inc(&ref_count_dispb);
	ret = tegra12x_unpowergate(id);

error_out:
	mutex_unlock(&tegra12x_powergate_disp_lock);
	return ret;
}

static int tegra12x_venc_powergate(int id)
{
	int ret = 0;
	int ref_count = 0;

	if (!TEGRA_IS_VENC_POWERGATE_ID(id))
		return -EINVAL;

	ref_count = atomic_dec_return(&ref_count_venc);
	WARN_ON(ref_count < 0);

	/* only powergate when decrementing ref_count from 1 to 0 */
	if (ref_count == 0) {
		CHECK_RET(tegra12x_powergate(id));
		CHECK_RET(tegra12x_disp_powergate(TEGRA_POWERGATE_DISA));
	}

	return ret;
}

static int tegra12x_venc_unpowergate(int id)
{
	int ret = 0;
	int ref_count = 0;

	if (!TEGRA_IS_VENC_POWERGATE_ID(id))
		return -EINVAL;

	ref_count = atomic_inc_return(&ref_count_venc);
	WARN_ON(ref_count < 1);

	/* only unpowergate when incrementing ref_count from 0 to 1 */
	if (ref_count == 1) {
		CHECK_RET(tegra12x_disp_unpowergate(TEGRA_POWERGATE_DISA));
		CHECK_RET(tegra12x_unpowergate(id));
	}

	return ret;
}

static int tegra12x_pcie_powergate(int id)
{
	int ret = 0;
	int ref_count = 0;

	if (!TEGRA_IS_PCIE_POWERGATE_ID(id))
		return -EINVAL;

	ref_count = atomic_dec_return(&ref_count_pcie);
	WARN_ON(ref_count < 0);

	/* only powergate when decrementing ref_count from 1 to 0 */
	if (ref_count == 0)
		CHECK_RET(tegra12x_powergate(id));

	return ret;
}

static int tegra12x_pcie_unpowergate(int id)
{
	int ret = 0;
	int ref_count = 0;

	if (!TEGRA_IS_PCIE_POWERGATE_ID(id))
		return -EINVAL;

	ref_count = atomic_inc_return(&ref_count_pcie);
	WARN_ON(ref_count < 1);

	/* only unpowergate when incrementing ref_count from 0 to 1 */
	if (ref_count == 1)
		CHECK_RET(tegra12x_unpowergate(id));

	return ret;
}

/*
 * Due to a HW bug 1320346 in t12x/t13x, PCIE needs to be unpowergated when
 * XUSB is to be accessed.  Since PCIE uses reference counter, we can attempt
 * to powergated/unpowergate PCIE when XUSB is powergated/unpowergated.  This
 * will ensure that PCIE is unpowergated when (either XUSB or PCIE) needs it
 * and will be powergated when (neither XUSB nor PCIE) needs it.
 */
static int tegra12x_xusbc_powergate(int id)
{
	int ret = 0;
	unsigned long val;
	bool iddq = false;
	void __iomem *clk_base = IO_ADDRESS(TEGRA_CLK_RESET_BASE);

	if (!TEGRA_IS_XUSBC_POWERGATE_ID(id))
		return -EINVAL;
	val = readl(clk_base + UTMIPLL_HW_PWRDN_CFG0);
	if (val & UTMIPLL_HW_PWRDN_CFG0_IDDQ_OVERRIDE)
		iddq = true;

	val &= ~UTMIPLL_HW_PWRDN_CFG0_IDDQ_OVERRIDE;
	val |= UTMIPLL_HW_PWRDN_CFG0_IDDQ_SWCTL;
	writel(val, clk_base + UTMIPLL_HW_PWRDN_CFG0);

	CHECK_RET(tegra12x_powergate(id));
	CHECK_RET(tegra12x_pcie_powergate(TEGRA_POWERGATE_PCIE));

	if (iddq) {
		val = readl(clk_base + UTMIPLL_HW_PWRDN_CFG0);
		val |= UTMIPLL_HW_PWRDN_CFG0_IDDQ_OVERRIDE |
			UTMIPLL_HW_PWRDN_CFG0_IDDQ_SWCTL;
		writel(val, clk_base + UTMIPLL_HW_PWRDN_CFG0);
	}


	return ret;
}

static int tegra12x_xusbc_unpowergate(int id)
{
	int ret = 0;

	if (!TEGRA_IS_XUSBC_POWERGATE_ID(id))
		return -EINVAL;

	CHECK_RET(tegra12x_unpowergate(id));
	CHECK_RET(tegra12x_pcie_unpowergate(TEGRA_POWERGATE_PCIE));

	return ret;
}

int tegra12x_powergate_partition(int id)
{
	int ret;

	if (TEGRA_IS_GPU_POWERGATE_ID(id)) {
		ret = tegra12x_gpu_powergate(id,
			&tegra12x_powergate_partition_info[id]);
	} else if (TEGRA_IS_DISP_POWERGATE_ID(id))
		ret = tegra12x_disp_powergate(id);
	else if (id == TEGRA_POWERGATE_CRAIL)
		ret = tegra_powergate_set(id, false);
	else if (id == TEGRA_POWERGATE_VENC)
		ret = tegra12x_venc_powergate(id);
	else if (id == TEGRA_POWERGATE_PCIE)
		ret = tegra12x_pcie_powergate(id);
	else if (id == TEGRA_POWERGATE_XUSBC)
		ret = tegra12x_xusbc_powergate(id);
	else {
		/* call common power-gate API for t1xx */
		ret = tegra1xx_powergate(id,
			&tegra12x_powergate_partition_info[id]);
	}

	return ret;
}

int tegra12x_unpowergate_partition(int id)
{
	int ret;

	if (TEGRA_IS_GPU_POWERGATE_ID(id)) {
		ret = tegra12x_gpu_unpowergate(id,
			&tegra12x_powergate_partition_info[id]);
	} else if (TEGRA_IS_DISP_POWERGATE_ID(id))
		ret = tegra12x_disp_unpowergate(id);
	else if (id == TEGRA_POWERGATE_CRAIL)
		ret = tegra_powergate_set(id, true);
	else if (id == TEGRA_POWERGATE_VENC)
		ret = tegra12x_venc_unpowergate(id);
	else if (id == TEGRA_POWERGATE_PCIE)
		ret = tegra12x_pcie_unpowergate(id);
	else if (id == TEGRA_POWERGATE_XUSBC)
		ret = tegra12x_xusbc_unpowergate(id);
	else {
		ret = tegra1xx_unpowergate(id,
			&tegra12x_powergate_partition_info[id]);
	}

	return ret;
}

int tegra12x_powergate_partition_with_clk_off(int id)
{
	BUG_ON(TEGRA_IS_GPU_POWERGATE_ID(id));

	return tegra1xx_powergate_partition_with_clk_off(id,
		&tegra12x_powergate_partition_info[id]);
}

int tegra12x_unpowergate_partition_with_clk_on(int id)
{
	BUG_ON(TEGRA_IS_GPU_POWERGATE_ID(id));

	return tegra1xx_unpowergate_partition_with_clk_on(id,
		&tegra12x_powergate_partition_info[id]);
}

const char *tegra12x_get_powergate_domain_name(int id)
{
	return tegra12x_powergate_partition_info[id].name;
}

spinlock_t *tegra12x_get_powergate_lock(void)
{
	return &tegra12x_powergate_lock;
}

bool tegra12x_powergate_skip(int id)
{
	switch (id) {
	case TEGRA_POWERGATE_GPU:
		return true;
	default:
		return false;
	}
}

bool tegra12x_powergate_is_powered(int id)
{
	u32 status = 0;

	if (TEGRA_IS_GPU_POWERGATE_ID(id)) {
		if (gpu_rail)
			return tegra_dvfs_is_rail_up(gpu_rail);
	} else {
		status = pmc_read(PWRGATE_STATUS) & (1 << id);
		return !!status;
	}
	return status;
}

static int tegra12x_powergate_init_refcount(void)
{
	bool disa_powered = tegra_powergate_is_powered(TEGRA_POWERGATE_DISA);
	bool venc_powered = tegra_powergate_is_powered(TEGRA_POWERGATE_VENC);
	bool pcie_powered = tegra_powergate_is_powered(TEGRA_POWERGATE_PCIE);

	WARN_ON(venc_powered && !disa_powered);

	/* if it wasn't powered on, power it on */
	if (!disa_powered) {
		tegra12x_disp_unpowergate(TEGRA_POWERGATE_DISA);
	} else { /* if it was, set the refcount to 1 */
		atomic_set(&ref_count_dispa, 1);
	}
	/* either way you end up with disa powered on and the
	 * refcount set to 1 */

	if (venc_powered) {
		/* venc_unpowergate() take a ref_count on dispa to account for
		 * the hardware dependency between the two. This needs to
		 * happen here as well to match that behaviour.
		 */
		atomic_inc(&ref_count_dispa);
		atomic_set(&ref_count_venc, 1);
	} else {
		atomic_set(&ref_count_venc, 0);
	}

	/* PCIE needs refcount menchanism due to HW Bug#1320346.  PCIE should be
	 * powergated only when both XUSB and PCIE are not active.
	 */

	atomic_set(&ref_count_pcie, 0);

#ifdef CONFIG_ARCH_TEGRA_HAS_PCIE
	if (pcie_powered)
		atomic_inc(&ref_count_pcie);
	else {
		tegra12x_unpowergate_partition(TEGRA_POWERGATE_PCIE);
		pcie_powered = true;
	}
#endif

#ifdef CONFIG_TEGRA_XUSB_PLATFORM
	if (pcie_powered)
		atomic_inc(&ref_count_pcie);
	else
		tegra12x_unpowergate_partition(TEGRA_POWERGATE_PCIE);
#endif
	return 0;
}

static struct powergate_ops tegra12x_powergate_ops = {
	.soc_name = "tegra12x",

	.num_powerdomains = TEGRA_NUM_POWERGATE,

	.get_powergate_lock = tegra12x_get_powergate_lock,
	.get_powergate_domain_name = tegra12x_get_powergate_domain_name,

	.powergate_partition = tegra12x_powergate_partition,
	.unpowergate_partition = tegra12x_unpowergate_partition,

	.powergate_partition_with_clk_off =  tegra12x_powergate_partition_with_clk_off,
	.unpowergate_partition_with_clk_on = tegra12x_unpowergate_partition_with_clk_on,

	.powergate_mc_enable = tegra12x_powergate_mc_enable,
	.powergate_mc_disable = tegra12x_powergate_mc_disable,

	.powergate_mc_flush = tegra12x_powergate_mc_flush,
	.powergate_mc_flush_done = tegra12x_powergate_mc_flush_done,

	.powergate_skip = tegra12x_powergate_skip,

	.powergate_init_refcount = tegra12x_powergate_init_refcount,
	.powergate_is_powered = tegra12x_powergate_is_powered,
};

struct powergate_ops *tegra12x_powergate_init_chip_support(void)
{
	return &tegra12x_powergate_ops;
}
