/*-----------------------------------------------------------------------------
 * atemsys.c
 * Copyright (c) 2009 - 2019 acontis technologies GmbH, Weingarten, Germany
 * All rights reserved.
 *
 * This program is free software; you can redistribute  it and/or modify it
 * under  the terms of  the GNU General  Public License as published by the
 * Free Software Foundation;  either version 2 of the  License, or (at your
 * option) any later version.
 *
 * Response                  Paul Bussmann
 * Description               Provides usermode access to:
 *   - PCI configuration space
 *   - Device IO memory
 *   - Contiguous DMA memory
 *   - Single device interrupt
 *
 *
 * The driver should be used in the following way:
 *
 * - Make sure this driver's device node is present. I.e. call "mknod /dev/atemsys c 101 0"
 *
 * - open()
 *   Open driver (There can be more then one file descriptor active in parallel).
 *
 * - close()
 *   Close driver. Free resources, if any were allocated.
 *
 * - ioctl(ATEMSYS_IOCTL_PCI_FIND_DEVICE)
 *   Scan for PCI Devices.
 *   Input:  VendorID, DeviceID, InstanceNo
 *   Output: BusNo, DevNo, FuncNo
 *
 * - ioctl(ATEMSYS_IOCTL_PCI_CONF_DEVICE)
 *   Configures PCI device. This ioctl pins the given PCI device to the current filedescriptor.
 *   Input:  BusNo, DevNo, FuncNo
 *   Output: Physical IO base address, IO area length, IRQ number
 *   The device must be released explicitly in order to configure the next device. The ioctl gets
 *   errno EBUSY if the device is in use by another device driver.
 *
 * - ioctl(ATEMSYS_IOCTL_PCI_RELEASE_DEVICE)
 *   Release PCI device and free resources assigned to PCI device (interrupt, DMA memory, ...).
 *
 * - mmap(0, dwSize, PROT_READ | PROT_WRITE, MAP_SHARED | MAP_LOCKED, fd, 0);
 *   Allocates and maps DMA memory of size dwSize. Note that the last parameter (offset) must be 0.
 *   Input:  Length in byte
 *   Output: Pointer to the allocated memory and DMA physical address. On success this address is
 *           written into the first 4 bytes of the allocated memory.
 *
 * - mmap(0, IOphysSize, PROT_READ | PROT_WRITE, MAP_SHARED | MAP_LOCKED, fd, IOphysAddr);
 *   Maps IO memory of size IOphysSize.
 *   PCI device:
 *     First call ioctl(ATEMSYS_IOCTL_PCI_CONF_DEVICE). The IOphysAddr and IOphysSize
 *     parameter must corespond with the base IO address and size returned by
 *     ioctl(ATEMSYS_IOCTL_PCI_CONF_DEVICE), or the ioctl will fail.
 *   Non-PCI device:
 *     Don't call ioctl(ATEMSYS_IOCTL_PCI_CONF_DEVICE) before and just pass
 *     IOphysAddr and IOphysSize. There are no checks done.
 *   Input:  Phys.IO base address, IO area length in byte
 *   Output: Pointer to the mapped IO memory.
 *   The user should call dev_munmap() if the requested DMA memory is not needed anymore. In any cases
 *   the allocated / mapped memory is released / unmapped if the module is unloaded.
 *
 * - ioctl(ATEMSYS_IOCTL_INT_CONNECT)
 *   Connect an ISR to the device's interrupt.
 *   If the parameter is USE_PCI_INT, then the IRQ is taken from the selected PCI device.
 *   So in this case you have to call ioctl(ATEMSYS_IOCTL_PCI_CONF_DEVICE) first, or it will fail.
 *   Input:  IRQ-Number or USE_PCI_INT
 *   Output: none
 *   The device interrupt is active if this ioctl succeeds. The caller should do a read() on the file
 *   descriptor. The read call unblocks if an interrupt is received. If the read is unblocked, the
 *   interrupt is disabled on the (A)PIC and the caller must acknowledge the interrupt on the device
 *   (write to mmaped IO register). If the next read() is executed, the interrupt is enabled again
 *   on the (A)PIC. So a missing interrupt acknowledge will held the INT line active and interrupt
 *   trashing will happen (ISR is called again, read() unblocks, ...).
 *   Note that this ioctl will fail with errno EPERM if the interrupt line is shared.
 *   PCI device:
 *     The ioctl will try to use Message Signaled Interrupts (MSI) if supported
 *     by the PCI device. By definition, interrupts are never shared with MSI and MSI are mandatory
 *     for PCI-Express :).
 *
 * - ioctl(ATEMSYS_IOCTL_INT_DISCONNECT)
 *   Disconnect from device's interrupt.
 *
 * - ioctl(ATEMSYS_IOCTL_INT_INFO)
 *   Query used interrupt number.
 *
 * - read()
 *   see ioctl(ATEMSYS_IOCTL_INT_CONNECT)
 *
 *
 *  Changes see atemsys.h
 *
 *----------------------------------------------------------------------------*/

#define ATEMSYS_C

#include <linux/module.h>
#include "atemsys.h"
#include <linux/pci.h>

#if !(defined NO_IRQ) && (defined __aarch64__)
#define NO_IRQ   ((unsigned int)(-1))
#endif

#if (defined CONFIG_XENO_COBALT)
#include <rtdm/driver.h>
#else
#include <linux/version.h>
#include <linux/kernel.h>
#include <linux/fs.h>
#include <linux/interrupt.h>
#include <linux/semaphore.h>
#include <linux/mutex.h>
#include <linux/sched.h>
#include <linux/smp.h>
#if (LINUX_VERSION_CODE >= KERNEL_VERSION(4,11,00))
#include <linux/sched/signal.h>
#endif
#include <linux/irq.h>
#include <linux/list.h>
#if (defined CONFIG_OF)
#include <linux/of_device.h>
#endif

#if (LINUX_VERSION_CODE >= KERNEL_VERSION(4,13,0))
#include <linux/uaccess.h>
#else
#include <asm/uaccess.h>
#endif

#include <asm/current.h>
#include <linux/compat.h>
#include <linux/slab.h>
#include <linux/device.h>

#if ((defined CONFIG_OF) \
       && (LINUX_VERSION_CODE >= KERNEL_VERSION(4,9,0) /* not tested */)\
       && (!defined CONFIG_XENO_COBALT)  /* not tested */)
#define INCLUDE_ATEMSYS_DT_DRIVER    1
#include <linux/etherdevice.h>
#include <linux/clk.h>
#include <linux/phy.h>
#include <linux/clk/clk-conf.h>
#include <linux/pinctrl/consumer.h>
#include <linux/regulator/consumer.h>
#include <linux/pm_runtime.h>
#include <linux/of_mdio.h>
#include <linux/of_net.h>
#include <linux/delay.h>
#include <linux/kthread.h>
#include <linux/wait.h>
#include <asm/param.h>
#endif

#if (defined CONFIG_DTC)
#include <linux/of.h>
#include <linux/of_irq.h>
#endif /* CONFIG_DTC */
#endif /* CONFIG_XENO_COBALT */

#if (LINUX_VERSION_CODE >= KERNEL_VERSION(3,4,1))
#define INCLUDE_IRQ_TO_DESC
#endif

/* legacy support */
#if (LINUX_VERSION_CODE < KERNEL_VERSION(4,13,0))
#define wait_queue_entry_t wait_queue_t
#endif

#ifndef VM_RESERVED
#define VM_RESERVED (VM_DONTEXPAND | VM_DONTDUMP)
#endif


/* define this if IO memory should also be mapped into the kernel (for debugging only) */
#undef DEBUG_IOREMAP

MODULE_AUTHOR("acontis technologies GmbH <info@acontis.com>");
MODULE_DESCRIPTION("Generic usermode PCI driver");
MODULE_LICENSE("GPL");
MODULE_VERSION(ATEMSYS_VERSION_STR);

#if (LINUX_VERSION_CODE < KERNEL_VERSION(2,6,18))
#error "At least kernel version 2.6.18 is needed to compile!"
#endif

#if (defined CONFIG_XENO_COBALT)
#define PRINTK(prio, str, ...) rtdm_printk(prio ATEMSYS_DEVICE_NAME ": " str,  ##__VA_ARGS__)
#else
#define PRINTK(prio, str, ...) printk(prio ATEMSYS_DEVICE_NAME ": " str,  ##__VA_ARGS__)
#endif /* CONFIG_XENO_COBALT */

static int loglevel = LOGLEVEL_INFO;
#define ERR(str, ...) (LOGLEVEL_ERR <= loglevel)?     PRINTK(KERN_ERR, str, ##__VA_ARGS__)     :0
#define WRN(str, ...) (LOGLEVEL_WARNING <= loglevel)? PRINTK(KERN_WARNING, str, ##__VA_ARGS__) :0
#define INF(str, ...) (LOGLEVEL_INFO <= loglevel)?    PRINTK(KERN_INFO, str, ##__VA_ARGS__)    :0
#define DBG(str, ...) (LOGLEVEL_DEBUG <= loglevel)?   PRINTK(KERN_INFO, str, ##__VA_ARGS__)   :0

module_param(loglevel, int, 0);
MODULE_PARM_DESC(loglevel, "Set log level default LOGLEVEL_INFO, see /include/linux/kern_levels.h");

#ifndef PAGE_UP
#define PAGE_UP(addr)   (((addr)+((PAGE_SIZE)-1))&(~((PAGE_SIZE)-1)))
#endif
#ifndef PAGE_DOWN
#define PAGE_DOWN(addr) ((addr)&(~((PAGE_SIZE)-1)))
#endif

/* Comments: for kernel 2.6.18 add DMA_BIT_MASK*/
#ifndef DMA_BIT_MASK
#define DMA_BIT_MASK(n) (((n) == 64) ? ~0ULL : ((1ULL<<(n))-1))
#endif

#if (LINUX_VERSION_CODE >= KERNEL_VERSION(5,0,0))
#define ACCESS_OK(type, addr, size)     access_ok(addr, size)
#else
#define ACCESS_OK(type, addr, size)     access_ok(type, addr, size)
#endif

typedef struct
{
    u32               irq;
    atomic_t          count;
    atomic_t          totalCount;
#if (defined CONFIG_XENO_COBALT)
    rtdm_irq_t        irq_handle;
    rtdm_event_t      irq_event;
#else
    atomic_t          irqStatus;
    wait_queue_head_t q;
#endif /* CONFIG_XENO_COBALT */
#if (defined INCLUDE_IRQ_TO_DESC)
    bool              irq_is_level;
#endif
} irq_proc;

typedef struct
{
   struct list_head list;
#if (defined CONFIG_PCI)
   struct pci_dev  *pPcidev;
#endif
   irq_proc         irqDesc;
} dev_node;

typedef struct
{
   struct list_head  list;
#if (defined CONFIG_PCI)
   struct pci_dev   *pPcidev;
#endif
   dma_addr_t        dmaAddr;
   void             *pVirtAddr;
   size_t            len;
} mmap_node;

#if (defined CONFIG_OF)
#define ATEMSYS_DT_DRIVER_NAME "atemsys"
/* udev auto-loading support via DTB */
static const struct of_device_id atemsys_ids[] = {
    { .compatible = ATEMSYS_DT_DRIVER_NAME },
    { /* sentinel */ }
};
MODULE_DEVICE_TABLE(of, atemsys_ids);
#endif /* CONFIG_OF */

#if (defined INCLUDE_ATEMSYS_DT_DRIVER)
    #define ATEMSYS_MAX_NUMBER_OF_ETHERNET_PORTS 10
    #define ATEMSYS_MAX_NUMBER_OF_CLOCKS 10

    typedef struct {
        int                         nDev_id;
        struct net_device*          netdev;
        struct platform_device*     pPDev;
        struct device_node*         pDevNode;

        /* storage and identification */
        ATEMSYS_T_MAC_INFO          MacInfo;

        /* clocks */
        const char*                 clk_ids[ATEMSYS_MAX_NUMBER_OF_CLOCKS];
        struct clk*                 clks[ATEMSYS_MAX_NUMBER_OF_CLOCKS];
        int                         nCountClk;

        /* PHY */
        ATEMSYS_T_PHY_INFO          PhyInfo;
        phy_interface_t             PhyInterface;
        struct device_node*         pPhyNode;
        struct phy_device*          pPhyDev;
        struct regulator*           pPhyRegulator;
        struct task_struct*         etx_thread_StartPhy;
        struct task_struct*         etx_thread_StopPhy;

        /* mdio */
        ATEMSYS_T_MDIO_ORDER        MdioOrder;
        struct mii_bus*             pMdioBus;
        struct mutex                mdio_order_mutex;
        struct mutex                mdio_mutex;
        wait_queue_head_t           mdio_wait_queue;
        int                         mdio_wait_queue_flag;

        /* frame descriptor of the EcMaster connection */
        dev_node*                   pDevDesc;

    } ATEMSYS_T_ETH_DRV_DESC_PRIVATE;

    static ATEMSYS_T_ETH_DRV_DESC_PRIVATE*  S_apEthDrvDescPrivate[ATEMSYS_MAX_NUMBER_OF_ETHERNET_PORTS];

    static int StartPhyThread(void* pvData);
    static int StopPhyThread(void* pvData);
    static int CleanUpEthernetDriverOnRelease(dev_node* pDevDesc);
    static int GetMacInfoIoctl(dev_node* pDevDesc, unsigned long ioctlParam);
    static int PhyStartStopIoctl( unsigned long ioctlParam);
    static int GetMdioOrderIoctl(unsigned long ioctlParam);
    static int ReturnMdioOrderIoctl(unsigned long ioctlParam);
    static int GetPhyInfoIoctl(unsigned long ioctlParam);
    static int EthernetDriverRemove(struct platform_device *pPDev);
    static int EthernetDriverProbe(struct platform_device *pPDev);
#endif /* INCLUDE_ATEMSYS_DT_DRIVER */


static void dev_munmap(struct vm_area_struct *vma);

#if (defined CONFIG_XENO_COBALT)
   static int dev_interrupt_handler(rtdm_irq_t *irq_handle);
#else
   static irqreturn_t dev_interrupt_handler(int nIrq, void *pParam);
#endif /* CONFIG_XENO_COBALT */

static struct vm_operations_struct mmap_vmop =
{
   .close = dev_munmap,
};

#if (!defined CONFIG_XENO_COBALT)
static DEFINE_MUTEX(S_mtx);
static dev_node S_devNode;
static struct class* S_devClass;
static struct device* S_dev;

static void dev_enable_irq(irq_proc* pIp)
{
    /* enable/disable level type interrupts, not edge type interrupts */
#if (defined INCLUDE_IRQ_TO_DESC)
    if (pIp->irq_is_level)
#endif
    {
        atomic_inc(&pIp->irqStatus);
        enable_irq(pIp->irq);
    }
}

static void dev_disable_irq(irq_proc* pIp)
{
    /* enable/disable level type interrupts, not edge type interrupts */
#if (defined INCLUDE_IRQ_TO_DESC)
    if (!pIp->irq_is_level) return;
#endif

    if (atomic_read(&pIp->irqStatus) > 0)
    {
        disable_irq_nosync(pIp->irq);
        atomic_dec(&pIp->irqStatus);
    }
}

static int dev_irq_disabled(irq_proc* pIp)
{
    /* only level type interrupts get disabled */
#if (defined INCLUDE_IRQ_TO_DESC)
    if (!pIp->irq_is_level) return 0;
#endif

    if (atomic_read(&pIp->irqStatus) == 0)
    {
        return 1;
    }
    return 0;
}
#endif /* !CONFIG_XENO_COBALT */

#if (!defined __arm__) && (!defined __aarch64__)
static void * dev_dma_alloc(u32 dwLen, dma_addr_t *pDmaAddr)
{
   unsigned long virtAddr;
   unsigned long tmpAddr;
   u32 tmpSize;

   virtAddr =  __get_free_pages(GFP_KERNEL | GFP_DMA, get_order(dwLen));
   if (! virtAddr)
   {
      ERR("mmap: __get_free_pages failed\n");
      return NULL;
   }

   tmpAddr = virtAddr;
   tmpSize = dwLen;

   while (tmpSize > 0)
   {
     SetPageReserved( virt_to_page(tmpAddr) );
     tmpAddr += PAGE_SIZE;
     tmpSize -= PAGE_SIZE;
   }

   *pDmaAddr = virt_to_phys((void *) virtAddr);

   return (void *) virtAddr;
}

static void dev_dma_free(u32 dwLen, void *virtAddr)
{
   unsigned long tmpAddr = (unsigned long) virtAddr;
   u32 tmpSize = dwLen;

   while (tmpSize > 0)
   {
     ClearPageReserved( virt_to_page(tmpAddr) );
     tmpAddr += PAGE_SIZE;
     tmpSize -= PAGE_SIZE;
   }

   free_pages((unsigned long) virtAddr, get_order(dwLen));
}
#endif /* !__arm__ */

static void dev_munmap(struct vm_area_struct *vma)
{
   mmap_node *pMnode = (mmap_node *) vma->vm_private_data;

   INF("dev_munmap: 0x%p -> 0x%p (%d)\n",
         (void *) pMnode->pVirtAddr, (void *)(unsigned long)pMnode->dmaAddr, (int) pMnode->len);
    if (0 == pMnode->dmaAddr) { INF("dev_munmap: 0 == pMnode->dmaAddr!\n"); return; }
    if (NULL == pMnode->pVirtAddr) { INF("dev_munmap: NULL == pMnode->pVirtAddr!\n"); return; }

   /* free DMA memory */
#if (defined CONFIG_PCI)
   if (pMnode->pPcidev == NULL)
#endif
   {
#if (defined __arm__) || (defined __aarch64__)
      dma_free_coherent(S_dev, pMnode->len, pMnode->pVirtAddr, pMnode->dmaAddr);
#else
      dev_dma_free(pMnode->len, pMnode->pVirtAddr);
#endif
   }
#if (defined CONFIG_PCI)
   else
   {
#if ((defined __aarch64__) || (LINUX_VERSION_CODE >= KERNEL_VERSION(5,0,0)) \
    || ((defined __arm__) && (LINUX_VERSION_CODE >= KERNEL_VERSION(4,18,0))))
      dma_free_coherent(&pMnode->pPcidev->dev, pMnode->len, pMnode->pVirtAddr, pMnode->dmaAddr);
#else
      pci_free_consistent(pMnode->pPcidev, pMnode->len, pMnode->pVirtAddr, pMnode->dmaAddr);
#endif /* __aarch64__ */
   }
#endif /* CONFIG_PCI */
   kfree(pMnode);
}

#if (defined CONFIG_PCI)
/*
 * Lookup PCI device
 */
#if (LINUX_VERSION_CODE < KERNEL_VERSION(2,6,24))
struct pci_dev *pci_get_bus_and_slot(unsigned int bus, unsigned int devfn)
{
    struct pci_dev *dev = NULL;

    for_each_pci_dev(dev) {
        if (pci_domain_nr(dev->bus) == 0 &&
            (dev->bus->number == bus && dev->devfn == devfn))
            return dev;
    }
    return dev;
}
#endif

static int dev_pci_select_device(dev_node* pDevDesc, PCI_SELECT_DESC* pciDesc, size_t size)
{
   int nRetval = -EFAULT;
   s32 nPciBus, nPciDev, nPciFun;
   s32 nPciDomain = 0;

   get_user(nPciBus, &pciDesc->nPciBus);
   get_user(nPciDev, &pciDesc->nPciDev);
   get_user(nPciFun, &pciDesc->nPciFun);
   if (size == sizeof(PCI_SELECT_DESC) )
   {
      get_user(nPciDomain, &pciDesc->nPciDomain);
   }

   INF("pci_select: %04x:%02x:%02x.%x\n",
         (u32) nPciDomain, (u32) nPciBus, (u32) nPciDev, (u32) nPciFun);

   /* Lookup pci_dev object */
   pDevDesc->pPcidev = pci_get_domain_bus_and_slot(nPciDomain, nPciBus, PCI_DEVFN(nPciDev, nPciFun));
   if (pDevDesc->pPcidev == NULL)
   {
      WRN("pci_select: PCI-Device  %04x:%02x:%02x.%x not found\n",
            (unsigned) nPciDomain, (unsigned) nPciBus, (unsigned) nPciDev, (unsigned) nPciFun);
      goto Exit;
   }

   nRetval = DRIVER_SUCCESS;

Exit:
   return nRetval;
}

/*
 * See also kernel/Documentation/PCI/pci.txt for the recommended PCI initialization sequence
 */
static int ioctl_pci_configure_device(dev_node* pDevDesc, unsigned long ioctlParam, size_t size)
{
   int nRetval = -EIO;
   int nRc;
   int i;
   unsigned long ioBase;
   u32 dwIOLen;
   s32 nBar = 0;
   PCI_SELECT_DESC *pPciDesc = (PCI_SELECT_DESC *) ioctlParam;

   if (!ACCESS_OK(VERIFY_WRITE, pPciDesc, sizeof(PCI_SELECT_DESC)))
   {
      ERR("pci_conf: EFAULT\n");
      nRetval = -EFAULT;
      goto Exit;
   }

   if (pDevDesc->pPcidev != NULL)
   {
      WRN("pci_conf: error call ioctl(ATEMSYS_IOCTL_PCI_RELEASE_DEVICE) first\n");
      goto Exit;
   }

   if (dev_pci_select_device(pDevDesc, pPciDesc, size) != DRIVER_SUCCESS)
   {
      goto Exit;
   }

   /* enable device */
   nRc = pci_enable_device(pDevDesc->pPcidev);
   if (nRc < 0)
   {
      ERR("pci_conf: pci_enable_device failed\n");
      pDevDesc->pPcidev = NULL;
      goto Exit;
   }

   /* Check if IO-memory is in use by another driver */
   nRc = pci_request_regions(pDevDesc->pPcidev, ATEMSYS_DEVICE_NAME);
   if (nRc < 0)
   {
      ERR("pci_conf: device \"%s\" in use by another driver?\n", pci_name(pDevDesc->pPcidev));
      pDevDesc->pPcidev = NULL;
      nRetval = -EBUSY;
      goto Exit;
   }

   /* find the memory BAR */
   for (i = 0; i < ATEMSYS_PCI_MAXBAR ; i++)
   {
      if (pci_resource_flags(pDevDesc->pPcidev, i) & IORESOURCE_MEM)
      {
         /* IO area address */
         ioBase = pci_resource_start(pDevDesc->pPcidev, i);

         if (ioBase > 0xFFFFFFFF)
         {
            ERR("pci_conf: 64-Bit IO address not supported\n");
            pDevDesc->pPcidev = NULL;
            nRetval = -ENODEV;
            goto Exit;
         }

         put_user((u32)ioBase, &(pPciDesc->aBar[nBar].dwIOMem));

         /* IO area length */
         dwIOLen = pci_resource_len(pDevDesc->pPcidev, i);
         put_user(dwIOLen, &(pPciDesc->aBar[nBar].dwIOLen));

         nBar++;
      }
   }

   if (nBar == 0)
   {
      ERR("pci_conf: No memory BAR found\n");
      pDevDesc->pPcidev = NULL;
      nRetval = -ENODEV;
      goto Exit;
   }

   put_user(nBar, &(pPciDesc->nBarCnt)); /* number of memory BARs */

   /* Turn on Memory-Write-Invalidate if it is supported by the device*/
#if (LINUX_VERSION_CODE < KERNEL_VERSION(2,6,24))
   pci_set_mwi(pDevDesc->pPcidev);
#else
   pci_try_set_mwi(pDevDesc->pPcidev);
#endif

#if (defined CONFIG_OF)
 #if (LINUX_VERSION_CODE >= KERNEL_VERSION(4,18,0))
   of_dma_configure(&pDevDesc->pPcidev->dev,pDevDesc->pPcidev->dev.of_node, true);
 #else
   of_dma_configure(&pDevDesc->pPcidev->dev,pDevDesc->pPcidev->dev.of_node);
 #endif
#endif

   /* Set DMA mask. We can handle only 32 bit DMA addresses! */
   nRc = pci_set_dma_mask(pDevDesc->pPcidev, DMA_BIT_MASK(32));
   if (nRc < 0)
   {
      ERR("pci_conf: pci_set_dma_mask failed\n");
      pci_release_regions(pDevDesc->pPcidev);
      pDevDesc->pPcidev = NULL;
      goto Exit;
   }
   nRc = pci_set_consistent_dma_mask(pDevDesc->pPcidev, DMA_BIT_MASK(32));
   if (nRc < 0)
   {
      ERR("pci_conf: pci_set_consistent_dma_mask failed\n");
      pci_release_regions(pDevDesc->pPcidev);
      pDevDesc->pPcidev = NULL;
      goto Exit;
   }

   /* Enable bus master DMA */
   pci_set_master(pDevDesc->pPcidev);

   /* Try to enable MSI (Message Signaled Interrupts). MSI's are non shared, so we can
    * use interrupt mode, also if we have a non exclusive interrupt line with legacy
    * interrupts.
    */
   if (pci_enable_msi(pDevDesc->pPcidev))
   {
      INF("pci_conf: legacy INT configured for device %s\n", pci_name(pDevDesc->pPcidev));
   }
   else
   {
      INF("pci_conf: MSI configured for device %s\n", pci_name(pDevDesc->pPcidev));
   }

   put_user((u32)pDevDesc->pPcidev->irq, &(pPciDesc->dwIrq)); /* assigned IRQ */

#if defined(__arm__) && 0
   /*
    * This is required for TI's TMDXEVM8168 (Cortex A8) eval board
    * \sa TI "DM81xx AM38xx PCI Express Root Complex Driver User Guide"
    * "DM81xx RC supports maximum remote read request size (MRRQS) as 256 bytes"
    */
   pcie_set_readrq(pDevDesc->pPcidev, 256);
#endif

   nRetval = 0;

Exit:
   return nRetval;
}

static int ioctl_pci_finddevice(dev_node* pDevDesc, unsigned long ioctlParam, size_t size)
{
   int nRetval = -EIO;
   struct pci_dev* pPciDev = NULL;
   s32 nVendor, nDevice, nInstance, nInstanceId;
   PCI_SELECT_DESC* pPciDesc = (PCI_SELECT_DESC *) ioctlParam;

   if (!ACCESS_OK(VERIFY_WRITE, pPciDesc, sizeof(PCI_SELECT_DESC)))
   {
      ERR("pci_find: EFAULT\n");
      nRetval = -EFAULT;
      goto Exit;
   }

   get_user(nVendor, &pPciDesc->nVendID);
   get_user(nDevice, &pPciDesc->nDevID);
   get_user(nInstance, &pPciDesc->nInstance);

   INF("pci_find: ven 0x%x dev 0x%x nInstance %d\n", nVendor, nDevice, nInstance);

   for (nInstanceId = 0; nInstanceId <= nInstance; nInstanceId++ )
   {
      pPciDev = pci_get_device (nVendor, nDevice, pPciDev);
   }

   if (pPciDev == NULL)
   {
      WRN("pci_find: device 0x%x:0x%x:%d not found\n", nVendor, nDevice, nInstance);
      nRetval = -ENODEV;
      goto Exit;
   }

   INF("pci_find: found 0x%x:0x%x:%d -> %s\n",
       nVendor, nDevice, nInstance, pci_name(pPciDev));

   put_user((s32)pPciDev->bus->number, &pPciDesc->nPciBus); /* Bus */
   put_user((s32)PCI_SLOT(pPciDev->devfn), &pPciDesc->nPciDev); /* Device */
   put_user((s32)PCI_FUNC(pPciDev->devfn), &pPciDesc->nPciFun); /* Function */
   if (size == sizeof(PCI_SELECT_DESC) )
   {
      put_user((s32)pci_domain_nr(pPciDev->bus), &pPciDesc->nPciDomain); /* Domain */
   }

   nRetval = 0;

Exit:
   return nRetval;
}
#endif /* CONFIG_PCI */

#if (defined CONFIG_DTC)
/*
 * Lookup Nth (0: first) compatible device tree node with "interrupts" property present.
 */
static struct device_node * atemsys_of_lookup_intnode(const char *compatible, int deviceIdx)
{
   struct device_node *device = NULL;
   struct device_node *child = NULL;
   struct device_node *tmp = NULL;
   int devCnt;

   /* Lookup Nth device tree node */
   devCnt = 0;
   for_each_compatible_node(tmp, NULL, compatible)
   {
      if (devCnt == deviceIdx)
      {
         device = tmp;
         break;
      }
      ++devCnt;
   }

   if (device == NULL) return NULL;

   if (of_get_property(device, "interrupts", NULL)) return device;

   /* i.e. vETSEC has 2 groups. Search them */
   for_each_child_of_node(device, child)
   {
      if (of_get_property(child, "interrupts", NULL)) return child;
   }

   return NULL;
}

/*
 * Map interrupt number taken from the OF Device Tree (\sa .dts file) into
 * virtual interrupt number which can be passed to request_irq().
 * The usual (device driver) way is to use the irq_of_parse_and_map() function.
 *
 * We search all device tree nodes which have the "compatible" property
 * equal to compatible. Search until the Nth device is found. Then
 * map the Nth interrupt (given by intIdx) with irq_of_parse_and_map().
 */
static unsigned atemsys_of_map_irq_to_virq(const char *compatible, int deviceIdx, int intIdx)
{
   unsigned virq;
   struct device_node *device = NULL;

   /* Lookup Nth device */
   device = atemsys_of_lookup_intnode(compatible, deviceIdx);
   if (! device)
   {
      ERR("atemsys_of_map_irq_to_virq: device tree node '%s':%d not found.\n",
         compatible, deviceIdx);
      return NO_IRQ;
   }

   virq = irq_of_parse_and_map(device, intIdx);
   if (virq == NO_IRQ)
   {
      ERR("atemsys_of_map_irq_to_virq: irq_of_parse_and_map failed for"
          " device tree node '%s':%d, IntIdx %d.\n",
         compatible, deviceIdx, intIdx);
   }

   return virq;
}
#endif /* CONFIG_DTC */

#if (defined INCLUDE_IRQ_TO_DESC)
static bool atemsys_irq_is_level(unsigned int irq_id)
{
    struct irq_desc *desc;
    bool irq_is_level = true;

    // desc = irq_to_desc(irq_id); // 5.10之后的内核不支持irq_to_desc()
                                   // 参考https://github.com/gschorcht/i2c-ch341-usb/pull/9/files
    desc = irq_data_to_desc(irq_get_irq_data(irq_id));
    if (desc)
    {
        irq_is_level = irqd_is_level_type(&desc->irq_data);
    }

    return irq_is_level;
}
#endif /* INCLUDE_IRQ_TO_DESC */

static int ioctl_int_connect(dev_node* pDevDesc, unsigned long ioctlParam)
{
    int nRetval = -EIO;
    int nRc;
    irq_proc *pIp = NULL;
    unsigned int irq = 0;

#if (defined CONFIG_PCI)
    if (ioctlParam == USE_PCI_INT)
    {
        /* Use IRQ number from selected PCI device */

        if (pDevDesc->pPcidev == NULL)
        {
            WRN("intcon: error call ioctl(ATEMSYS_IOCTL_PCI_CONF_DEVICE) first\n");
            goto Exit;
        }

        irq = pDevDesc->pPcidev->irq;
        INF("intcon: Use IRQ (%d) from PCI config\n", irq);
    }
    else
#endif /* CONFIG_PCI */
    {
#if (defined CONFIG_DTC)
        /* The ioctlParam is the Nth compatible device in the OF device tree (0: first, 1: second, ...)
         * TODO "compatible string" and "interrupt index" should be provided by usermode as IOCTL param
         */
        if ( /* Use interrupt number at idx 1 (Rx-Interrupt) for TSEC / eTSEC */
             ((irq = atemsys_of_map_irq_to_virq("fsl,etsec2", ioctlParam, 1)) == NO_IRQ) /* PPC, eTSEC */
          && ((irq = atemsys_of_map_irq_to_virq("gianfar", ioctlParam, 1)) == NO_IRQ) /* PPC, eTSEC */
          /* PRU-ICSS for am572x, am335x */
          && ((irq = atemsys_of_map_irq_to_virq("acontis,device", 0, ioctlParam)) == NO_IRQ)
          /* Use interrupt number at idx 0 (Catch-All-Interrupt) for GEM */
          && ((irq = atemsys_of_map_irq_to_virq("xlnx,ps7-ethernet-1.00.a", ioctlParam, 0)) == NO_IRQ) /* ARM, Xilinx Zynq */
           )
        {
            nRetval = -EPERM;
            goto Exit;
        }
        INF("intcon: Use IRQ (%d) from OF Device Tree\n", irq);
#else
        /* Use IRQ number passed as ioctl argument */
        irq = ioctlParam;
        INF("intcon: Use IRQ (%d) passed by user\n", irq);
#endif
    }

    pIp = &pDevDesc->irqDesc;
    if (pIp->irq)
    {
        WRN("intcon: error IRQ %u already connected. Call ioctl(ATEMSYS_IOCTL_INT_DISCONNECT) first\n",
            (unsigned) pIp->irq);
        goto Exit;
    }

    /* Setup some data which is needed during Interrupt handling */
    memset(pIp, 0, sizeof(irq_proc));
    atomic_set(&pIp->count, 0);
    atomic_set(&pIp->totalCount, 0);

#if (defined CONFIG_XENO_COBALT)
    rtdm_event_init(&pIp->irq_event, 0);
    nRc = rtdm_irq_request(&pIp->irq_handle, irq, dev_interrupt_handler, 0, ATEMSYS_DEVICE_NAME, pDevDesc);
    if (nRc)
    {
        ERR("ioctl_int_connect: rtdm_irq_request() for IRQ %d returned error: %d\n", irq, nRc);
        nRetval = nRc;
        goto Exit;
    }
    nRc = rtdm_irq_enable(&pIp->irq_handle);
    if (nRc)
    {
        ERR("ioctl_int_connect: rtdm_irq_enable() for IRQ %d returned error: %d\n", irq, nRc);
        nRetval = nRc;
        goto Exit;
    }
#else
    init_waitqueue_head(&pIp->q);
    atomic_set(&pIp->irqStatus, 1); /* IRQ enabled */

    /* Setup non shared IRQ */
    nRc = request_irq(irq, dev_interrupt_handler, 0, ATEMSYS_DEVICE_NAME, pDevDesc);
    if (nRc)
    {
        ERR("ioctl_int_connect: request_irq (IRQ %d) failed. Err %d\n", irq, nRc);
        nRetval = -EPERM;
        goto Exit;
    }
#endif /* CONFIG_XENO_COBALT */

    pIp->irq = irq;
#if (defined INCLUDE_IRQ_TO_DESC)
    pIp->irq_is_level = atemsys_irq_is_level(irq);
#endif

#if (defined INCLUDE_IRQ_TO_DESC)
    INF("intcon: IRQ %d connected, irq_is_level = %d\n", irq, pIp->irq_is_level);
#else
    INF("intcon: IRQ %d connected\n", irq);
#endif

    nRetval = 0;
Exit:
    return nRetval;
}

static int ioctl_intinfo(dev_node* pDevDesc, unsigned long ioctlParam)
{
   int nRetval = -EIO;
   ATEMSYS_T_INT_INFO *pIntInfo = (ATEMSYS_T_INT_INFO *) ioctlParam;

#if (defined CONFIG_XENO_COBALT)
   struct rtdm_fd* fd = rtdm_private_to_fd(pDevDesc);
   if (rtdm_fd_is_user(fd))
   {
      nRetval = rtdm_safe_copy_to_user(fd, &pIntInfo->dwInterrupt, &pDevDesc->irqDesc.irq, sizeof(__u32));
      if (nRetval)
      {
          ERR("ioctl_intinfo failed: %d\n", nRetval);
          goto Exit;
      }
   }
#else
   if (!ACCESS_OK(VERIFY_WRITE, pIntInfo, sizeof(ATEMSYS_T_INT_INFO)))
   {
      ERR("ioctl_intinfo: EFAULT\n");
      nRetval = -EFAULT;
      goto Exit;
   }

   nRetval = put_user(pDevDesc->irqDesc.irq, &pIntInfo->dwInterrupt);
#endif /* CONFIG_XENO_COBALT */

Exit:
   return nRetval;
}


static int dev_int_disconnect(dev_node* pDevDesc)
{
   int nRetval = -EIO;
   int nCnt;
   irq_proc *pIp = &(pDevDesc->irqDesc);

#if (defined CONFIG_XENO_COBALT)
      int nRc;
      if (pIp->irq)
      {
         nRc = rtdm_irq_disable(&pIp->irq_handle);
         if (nRc)
         {
            ERR("dev_int_disconnect: rtdm_irq_disable() for IRQ %d returned error: %d\n", (u32) pIp->irq, nRc);
            nRetval = nRc;
            goto Exit;
         }

         nRc = rtdm_irq_free(&pIp->irq_handle);
         if (nRc)
         {
            ERR("dev_int_disconnect: rtdm_irq_free() for IRQ %d returned error: %d\n", (u32) pIp->irq, nRc);
            nRetval = nRc;
            goto Exit;
         }

         nCnt = atomic_read(&pIp->totalCount);
         INF("pci_intdcon: IRQ %u disconnected. %d interrupts rcvd\n", (u32) pIp->irq, nCnt);

         pIp->irq = 0;
         rtdm_event_signal(&pIp->irq_event);
      }
#else
      if (pIp->irq)
      {
         /* Disable INT line. We can call this, because we only allow exclusive interrupts */
         disable_irq_nosync(pIp->irq);

         /* Unregister INT routine.This will block until all pending interrupts are handled */
         free_irq(pIp->irq, pDevDesc);

         nCnt = atomic_read(&pIp->totalCount);
         INF("pci_intdcon: IRQ %u disconnected. %d interrupts rcvd\n", (u32) pIp->irq, nCnt);

         pIp->irq = 0;

         /* Wakeup sleeping threads -> read() */
         wake_up(&pIp->q);
      }
#endif /* CONFIG_XENO_COBALT */
   nRetval = 0;

#if (defined CONFIG_XENO_COBALT)
Exit:
#endif
   return nRetval;
}

#if (defined CONFIG_PCI)
static void dev_pci_release(dev_node* pDevDesc)
{
   if (pDevDesc->pPcidev)
   {
      pci_disable_device(pDevDesc->pPcidev);

#if (LINUX_VERSION_CODE >= KERNEL_VERSION(2,6,29))
      /* Make sure bus master DMA is disabled if the DMA buffers are finally released */
      pci_clear_master(pDevDesc->pPcidev);
#endif
      pci_release_regions(pDevDesc->pPcidev);

      pci_disable_msi(pDevDesc->pPcidev);

      INF("pci_release: PCI device %s released\n", pci_name(pDevDesc->pPcidev));

      pDevDesc->pPcidev = NULL;
   }
}
#endif /* CONFIG_PCI */

#if (defined CONFIG_XENO_COBALT)
static int dev_interrupt_handler(rtdm_irq_t *irq_handle)
{
    dev_node* pDevDesc = rtdm_irq_get_arg(irq_handle, dev_node);
    irq_proc* pIp = NULL;

    if (pDevDesc != NULL)
    {
        pIp = &(pDevDesc->irqDesc);
        if (pIp != NULL)
        {
            atomic_inc(&pIp->count);
            atomic_inc(&pIp->totalCount);
            rtdm_event_signal(&pIp->irq_event);
        }
    }
    return RTDM_IRQ_HANDLED;
}
#else
static irqreturn_t dev_interrupt_handler(int nIrq, void *pParam)
{
   dev_node* pDevDesc = (dev_node *) pParam;
   irq_proc* pIp = &(pDevDesc->irqDesc);

   /* Disable IRQ on (A)PIC to prevent interrupt trashing if the ISR is left.
    * In usermode the IRQ must be acknowledged on the device (IO register).
    * The IRQ is enabled again in the read() handler!
    * Just disabling the IRQ here doesn't work with shared IRQs!
    */
   dev_disable_irq(pIp);

   atomic_inc(&pIp->count);
   atomic_inc(&pIp->totalCount);

   /* Wakeup sleeping threads -> read() */
   wake_up(&pIp->q);

   return IRQ_HANDLED;
}
#endif /* CONFIG_XENO_COBALT */

/*
 * This is called whenever a process attempts to open the device file
 */
#if (defined CONFIG_XENO_COBALT)
static int device_open(struct rtdm_fd * fd, int oflags)
{
   dev_node* pDevDesc = (dev_node *) rtdm_fd_to_private(fd);
   memset(pDevDesc, 0, sizeof(dev_node));
   rtdm_event_init(&pDevDesc->irqDesc.irq_event, 0);
   INF("device_open %s\n", rtdm_fd_device(fd)->label);
#else
static int device_open(struct inode *inode, struct file *file)
{
   dev_node* pDevDesc;

   INF("device_open(0x%p)\n", file);

   /* create device descriptor */
   pDevDesc = (dev_node *) kzalloc(sizeof(dev_node), GFP_KERNEL);
   if (pDevDesc == NULL)
   {
      return -ENOMEM;
   }

   file->private_data = (void *) pDevDesc;

   /* Add descriptor to descriptor list */
   mutex_lock(&S_mtx);
   list_add(&pDevDesc->list, &S_devNode.list);
   mutex_unlock(&S_mtx);
   try_module_get(THIS_MODULE);
#endif /* CONFIG_XENO_COBALT */

   return DRIVER_SUCCESS;
}

#if (defined CONFIG_XENO_COBALT)
static void device_release(struct rtdm_fd * fd)
{
    dev_node* pDevDesc = (dev_node *) rtdm_fd_to_private(fd);
    irq_proc* pIp = NULL;
#else
static int device_release(struct inode *inode, struct file *file)
{
   dev_node* pDevDesc = file->private_data;
#endif /* CONFIG_XENO_COBALT */

   /* release device descriptor */
   if (pDevDesc != NULL )
   {
       INF("device_release, pDevDesc = 0x%p\n", pDevDesc);

       /* Try to tear down interrupts if they are on */
       dev_int_disconnect(pDevDesc);

#if (defined INCLUDE_ATEMSYS_DT_DRIVER)
       CleanUpEthernetDriverOnRelease(pDevDesc);
#endif

#if (defined CONFIG_PCI)
       /* Try to release PCI resources */
       dev_pci_release(pDevDesc);
#endif

#if (defined CONFIG_XENO_COBALT)
       pIp = &(pDevDesc->irqDesc);

       if (pIp != NULL )
       {
          rtdm_event_clear(&pIp->irq_event);
          rtdm_event_destroy(&pIp->irq_event);
       }
    }
    return;
#else
       /* Remove descriptor from descriptor list */
       mutex_lock(&S_mtx);

       list_del(&pDevDesc->list);

       mutex_unlock(&S_mtx);

       kfree(pDevDesc);
   }

   module_put(THIS_MODULE);

   return DRIVER_SUCCESS;
#endif /* CONFIG_XENO_COBALT */
}

/*
 * This function is called whenever a process which has already opened the
 * device file attempts to read from it.
 */
 #if (defined CONFIG_XENO_COBALT)
static ssize_t device_read(struct rtdm_fd * fd, void *bufp, size_t len)
{
   dev_node*   pDevDesc = (dev_node *) rtdm_fd_to_private(fd);
   irq_proc* pIp = NULL;
   s32 nPending;
   int ret=0;

   if (! pDevDesc)
   {
      return -EINVAL;
   }

   pIp = &(pDevDesc->irqDesc);
   if (! pIp)
   {
      return -EINVAL;
   }

   if (len < sizeof(u32))
   {
      return -EINVAL;
   }

   if (rtdm_in_rt_context() == false)
   {
       return -EINVAL;
   }

   if (rtdm_fd_is_user(fd) == false)
   {
       return -EINVAL;
   }

   ret = rtdm_event_wait(&pIp->irq_event);
   if (ret)
   {
       return ret;
   }

   nPending = atomic_read(&pIp->count);

   ret = rtdm_safe_copy_to_user(fd, bufp, &nPending, sizeof(nPending));

   if (ret)
   {
       ERR("device_read: rtdm_safe_copy_to_user() returned error: %d\n", ret);
       return ret;
   }

   atomic_sub(nPending, &pIp->count);

   return sizeof(nPending);
}
#else
static ssize_t device_read(
      struct file *filp,   /* see include/linux/fs.h   */
      char __user *bufp,   /* buffer to be filled with data */
      size_t       len,    /* length of the buffer     */
      loff_t      *ppos)
{

   dev_node* pDevDesc = (dev_node *) filp->private_data;
   irq_proc* pIp = NULL;
   s32 nPending;
   wait_queue_entry_t wait;

   if (! pDevDesc)
   {
      return -EINVAL;
   }

   pIp = &(pDevDesc->irqDesc);

   /* DBG("device_read...(0x%p,0x%p,%d)\n", filp, bufp, len); */

   init_wait(&wait);

   if (len < sizeof(u32))
   {
      return -EINVAL;
   }

   if (pIp->irq == 0) /* IRQ already disabled */
   {
      return -EINVAL;
   }

   nPending = atomic_read(&pIp->count);
   if (nPending == 0)
   {
      if (dev_irq_disabled(pIp))
      {
         dev_enable_irq(pIp);
      }
      if (filp->f_flags & O_NONBLOCK)
      {
         return -EWOULDBLOCK;
      }
   }

   while (nPending == 0)
   {
      prepare_to_wait(&pIp->q, &wait, TASK_INTERRUPTIBLE);
      nPending = atomic_read(&pIp->count);
      if (nPending == 0)
      {
         schedule();
      }
      finish_wait(&pIp->q, &wait);
      if (pIp->irq == 0) /* IRQ disabled while waiting for IRQ */
      {
         return -EINVAL;
      }
      if (signal_pending(current))
      {
         return -ERESTARTSYS;
      }
   }

   if (copy_to_user(bufp, &nPending, sizeof(nPending)))
   {
      return -EFAULT;
   }

   *ppos += sizeof(nPending);
   atomic_sub(nPending, &pIp->count);

   return sizeof(nPending);
}
#endif /* CONFIG_XENO_COBALT */

/*
 * character device mmap method
 */
#if (defined CONFIG_XENO_COBALT)
static int device_mmap(struct rtdm_fd * fd, struct vm_area_struct *vma)
{
   dev_node*   pDevDesc = (dev_node *) rtdm_fd_to_private(fd);
#else
static int device_mmap(struct file *filp, struct vm_area_struct *vma)
{
   dev_node*   pDevDesc = filp->private_data;
#endif /* CONFIG_XENO_COBALT */

   int         nRet = -EIO;
   u32         dwLen;
   void       *pVa = NULL;
   dma_addr_t  dmaAddr;
   mmap_node  *pMmapNode;
#if (defined CONFIG_PCI)
   int         i;
   unsigned long ioBase;
   u32 dwIOLen, dwPageOffset;
#endif

   DBG("mmap: vm_pgoff 0x%p vm_start = 0x%p vm_end = 0x%p\n",
         (void *) vma->vm_pgoff, (void *) vma->vm_start, (void *) vma->vm_end);

   if (pDevDesc == NULL)
   {
      ERR("mmap: Invalid device dtor\n");
      goto Exit;
   }

   dwLen = PAGE_UP(vma->vm_end - vma->vm_start);

   vma->vm_flags |= VM_RESERVED | VM_LOCKED | VM_DONTCOPY;

   if (vma->vm_pgoff != 0)
   {
      /* map device IO memory */
#if (defined CONFIG_PCI)
      if (pDevDesc->pPcidev != NULL)
      {
         INF("mmap: Doing PCI device sanity check\n");

         /* sanity check. Make sure that the offset parameter of the mmap() call in userspace
          * corresponds with the PCI base IO address.
          * Make sure the user doesn't map more IO memory than the device provides.
          */
         for (i = 0; i < ATEMSYS_PCI_MAXBAR; i++)
         {
            if (pci_resource_flags(pDevDesc->pPcidev, i) & IORESOURCE_MEM)
            {
               /* IO area address */
               ioBase = PAGE_DOWN( pci_resource_start(pDevDesc->pPcidev, i) );

               dwPageOffset = pci_resource_start(pDevDesc->pPcidev, i) - ioBase;

               /* IO area length */
               dwIOLen = PAGE_UP( pci_resource_len(pDevDesc->pPcidev, i) + dwPageOffset );

               if (    ((vma->vm_pgoff << PAGE_SHIFT) >= ioBase)
                    && (((vma->vm_pgoff << PAGE_SHIFT) + dwLen) <= (ioBase + dwIOLen))
                  )
               {
                  /* for systems where physical address is in x64 space, high dword is not passes from user io
                   * use correct address from pci_resource_start */
                  resource_size_t res_start = pci_resource_start(pDevDesc->pPcidev, i);
                  unsigned long pgoff_new = (res_start>>PAGE_SHIFT);
                  if (pgoff_new != vma->vm_pgoff)
                  {
                      INF("mmap: Correcting page offset from 0x%lx to 0x%lx, for Phys address 0x%llx",
                              vma->vm_pgoff, pgoff_new, (u64)res_start);
                      vma->vm_pgoff =  pgoff_new;
                  }

                  break;
               }
            }
         }

         /* IO bar not found? */
         if (i == ATEMSYS_PCI_MAXBAR)
         {
            ERR("mmap: Invalid arguments\n");
            nRet = -EINVAL;
            goto Exit;
         }
      }
#endif /* CONFIG_PCI */

      /* avoid swapping, request IO memory */
      vma->vm_flags |= VM_IO;

      /*
       * avoid caching (this is at least needed for POWERPC,
       * or machine will lock on first IO access)
       */
      vma->vm_page_prot = pgprot_noncached(vma->vm_page_prot);

      if ((nRet = remap_pfn_range(vma,
                                 vma->vm_start,
                                 vma->vm_pgoff,
                                 dwLen,
                                 vma->vm_page_prot)) < 0)
      {
         ERR("mmap: remap_pfn_range failed\n");
         goto Exit;
      }

      INF("mmap: mapped IO memory, Phys:0x%llx UVirt:0x%p Size:%u\n",
           (u64) (((u64)vma->vm_pgoff) << PAGE_SHIFT), (void *) vma->vm_start, dwLen);

#if (defined DEBUG_IOREMAP)
      {
        volatile unsigned char *ioaddr;
        unsigned long ioBase = vma->vm_pgoff << PAGE_SHIFT;
        INF("try to remap %p\n", (void *)ioBase);
        /* DEBUG Map device's IO memory into kernel space pagetables */
        ioaddr = (volatile unsigned char *) ioremap_nocache(ioBase, dwLen);
        if (ioaddr == NULL)
        {
          ERR("ioremap_nocache failed\n");
          goto Exit;
        }
        INF("io_base %p, *io_base[0]: %08x\n", ioaddr, readl(ioaddr));
      }
#endif /* DEBUG_IOREMAP */
   }
   else
   {
      /* allocated and map DMA memory */
#if (defined CONFIG_PCI)
      if (pDevDesc->pPcidev != NULL)
      {
#if ((defined __aarch64__) || (LINUX_VERSION_CODE >= KERNEL_VERSION(5,0,0)) \
    || ((defined __arm__) && (LINUX_VERSION_CODE >= KERNEL_VERSION(4,18,0))))
         pVa = dma_alloc_coherent(&pDevDesc->pPcidev->dev, dwLen, &dmaAddr, GFP_KERNEL);
         if (NULL == pVa)
         {
            ERR("mmap: dma_alloc_coherent failed\n");
            nRet = -ENOMEM;
            goto Exit;
         }
#else
         pVa = pci_alloc_consistent(pDevDesc->pPcidev, dwLen, &dmaAddr);
         if (NULL == pVa)
         {
            ERR("mmap: pci_alloc_consistent failed\n");
            nRet = -ENOMEM;
            goto Exit;
         }
#endif
      }
      else
#endif /* CONFIG_PCI */
      {
#if (defined __arm__) || (defined __aarch64__)
#if (defined CONFIG_OF)
         //S_dev->bus = &platform_bus_type;
 #if (LINUX_VERSION_CODE >= KERNEL_VERSION(4,18,0))
         of_dma_configure(S_dev,S_dev->of_node, true);
 #else
         of_dma_configure(S_dev,S_dev->of_node);
 #endif
#endif
         /* dma_alloc_coherent() is currently not tested on PPC.
          * TODO test this and remove legacy dev_dma_alloc()
          */
         pVa = dma_alloc_coherent(S_dev, dwLen, &dmaAddr, GFP_KERNEL);
         if (NULL == pVa)
         {
            ERR("mmap: dma_alloc_coherent failed\n");
            nRet = -ENOMEM;
            goto Exit;
         }
#else
         pVa = dev_dma_alloc(dwLen, &dmaAddr);
         if (NULL == pVa)
         {
            ERR("mmap: dev_dma_alloc failed\n");
            nRet = -ENOMEM;
            goto Exit;
         }
#endif
      }

      if (dmaAddr > 0xFFFFFFFF)
      {
         ERR("mmap: Can't handle 64-Bit DMA address\n");
         nRet = -ENOMEM;
         goto ExitAndFree;
      }

      /* zero memory for security reasons */
      memset(pVa, 0, dwLen);

      /* Always use noncached DMA memory for ARM. Otherwise cache invaliation/sync
       * would be necessary from usermode.
       * Can't do that without a kernel call because this OP's are privileged.
       */
#if (defined __arm__) || (defined __aarch64__)
      vma->vm_page_prot = pgprot_noncached(vma->vm_page_prot);
#endif

      /* map the whole physically contiguous area in one piece */
      if ((nRet = remap_pfn_range(vma,
                                 vma->vm_start,         /* User space virtual addr*/
                                 dmaAddr >> PAGE_SHIFT, /* physical page frame number */
                                 dwLen,                 /* size in bytes */
                                 vma->vm_page_prot)) < 0)
      {
         ERR("remap_pfn_range failed\n");
         goto ExitAndFree;
      }

      /* Write the physical DMA address into the first 4 bytes of allocated memory */
      *((u32 *) pVa) = (u32) dmaAddr;

      /* Some housekeeping to be able to cleanup the allocated memory later */
      pMmapNode = kzalloc(sizeof(mmap_node), GFP_KERNEL);
      if (! pMmapNode)
      {
         ERR("mmap: kmalloc() failed\n");
         nRet = -ENOMEM;
         goto ExitAndFree;
      }

#if (defined CONFIG_PCI)
      pMmapNode->pPcidev = pDevDesc->pPcidev;
#endif
      pMmapNode->dmaAddr = dmaAddr;
      pMmapNode->pVirtAddr = pVa;
      pMmapNode->len = dwLen;

      /* Setup close callback -> deallocates DMA memory if region is unmapped by the system */
      vma->vm_ops = &mmap_vmop;
      vma->vm_private_data = pMmapNode;

      INF("mmap: mapped DMA memory, Phys:0x%p KVirt:0x%p UVirt:0x%p Size:0x%x\n",
             (void *)(unsigned long)dmaAddr, (void *)pVa, (void *)vma->vm_start, dwLen);
   }

   nRet = 0;

   goto Exit;

ExitAndFree:

   if (pVa == NULL) goto Exit;

#if (defined CONFIG_PCI)
   if (pDevDesc->pPcidev != NULL)
   {
#if ((defined __aarch64__) || (LINUX_VERSION_CODE >= KERNEL_VERSION(5,0,0)) \
    || ((defined __arm__) && (LINUX_VERSION_CODE >= KERNEL_VERSION(4,18,0))))
      dma_free_coherent(&pDevDesc->pPcidev->dev, dwLen, pVa, dmaAddr);
#else
      pci_free_consistent(pDevDesc->pPcidev, dwLen, pVa, dmaAddr);
#endif
   }
   else
#endif
   {
#if (defined __arm__) || (defined __aarch64__)
      dma_free_coherent(S_dev, dwLen, pVa, dmaAddr);
#else
      dev_dma_free(dwLen, pVa);
#endif
   }

Exit:
   return nRet;
}

#if (defined(__GNUC__) && (defined(__ARM__) || defined(__arm__) || defined(__aarch64__)))
static void ioctl_enableCycleCount(void* arg)
{
   __u32 dwEnableUserMode = *(__u32*)arg;
   /* Make CCNT accessible from usermode */
#if !defined(__aarch64__)
   __asm__ __volatile__("mcr p15, 0, %0, c9, c14, 0" :: "r"(dwEnableUserMode));
#else
   /* aarch32: PMUSERENR => aarch64: PMUSERENR_EL0 */
   __asm__ __volatile__("msr PMUSERENR_EL0, %0" :: "r"(dwEnableUserMode));
#endif

   if (dwEnableUserMode)
   {
#if !defined(__aarch64__)
      /* Disable counter flow interrupt */
      __asm__ volatile ("mcr p15, 0, %0, c9, c14, 2" :: "r"(0x8000000f));
      /* Initialize CCNT */
      __asm__ volatile ("mcr p15, 0, %0, c9, c12, 0" :: "r"(5));
      /* Start CCNT */
      __asm__ volatile ("mcr p15, 0, %0, c9, c12, 1" :: "r"(0x80000000));
#else
      /* Disable counter flow interrupt */  /* aarch32:PMINTENCLR => aarch64:PMINTENCLR_EL1 */
      __asm__ volatile ("msr PMINTENCLR_EL1, %0" :: "r"(0x8000000f));
      /* Initialize CCNT */  /* aarch32:PMCR       => aarch64:PMCR_EL0*/
      __asm__ volatile ("msr PMCR_EL0, %0" :: "r"(5));
      /* Start CCNT */  /*  aarch32:PMCNTENSET => aarch64:PMCNTENSET_EL0 */
      __asm__ volatile ("msr PMCNTENSET_EL0, %0" :: "r"(0x80000000));
#endif
   }
   else
   {
#if !defined(__aarch64__)
      __asm__ volatile ("mcr p15, 0, %0, c9, c12, 0" :: "r"(0));
#else
      /* aarch32:PMCR       => aarch64:PMCR_EL0 */
      __asm__ volatile ("msr PMCR_EL0, %0" :: "r"(0));
#endif
   }
}
#endif

/*
 * This function is called whenever a process tries to do an ioctl on our
 * device file.
 *
 * If the ioctl is write or read/write (meaning output is returned to the
 * calling process), the ioctl call returns the output of this function.
 *
 */
#if (defined CONFIG_XENO_COBALT)
static int atemsys_ioctl(struct rtdm_fd * fd, unsigned int cmd, void __user *user_arg)
{
   dev_node*       pDevDesc = (dev_node *) rtdm_fd_to_private(fd);
   unsigned long   arg = (unsigned long) user_arg;
#else
static long atemsys_ioctl(
      struct file *file,
      unsigned int cmd,
      unsigned long arg)
{
   dev_node*        pDevDesc = file->private_data;
#endif /* CONFIG_XENO_COBALT */

   int              nRetval = -EFAULT;

   if (pDevDesc == NULL)
   {
      ERR("ioctl: Invalid device dtor\n");
      goto Exit;
   }

   /*
    * Switch according to the ioctl called
    */
   switch (cmd)
   {
#if (defined CONFIG_PCI)
      case ATEMSYS_IOCTL_PCI_FIND_DEVICE:
      case ATEMSYS_IOCTL_PCI_FIND_DEVICE_v1_3_04:
      {
         nRetval = ioctl_pci_finddevice(pDevDesc, arg, _IOC_SIZE(cmd));
         if (0 != nRetval)
         {
           /* be quiet. ioctl may fail */
           goto Exit;
         }
      } break;

      case ATEMSYS_IOCTL_PCI_CONF_DEVICE:
      case ATEMSYS_IOCTL_PCI_CONF_DEVICE_v1_3_04:
      {
         nRetval = ioctl_pci_configure_device(pDevDesc, arg, _IOC_SIZE(cmd));
         if (0 != nRetval)
         {
            ERR("ioctl ATEMSYS_IOCTL_PCI_CONF_DEVICE failed: %d\n", nRetval);
            goto Exit;
         }
      } break;

      case ATEMSYS_IOCTL_PCI_RELEASE_DEVICE:
      {
         if (pDevDesc->pPcidev == NULL)
         {
            DBG("pci_release: No PCI device selected. Call ioctl(ATEMSYS_IOCTL_PCI_CONF_DEVICE) first\n");
            goto Exit;
         }

         dev_pci_release(pDevDesc);
      } break;
#endif
      case ATEMSYS_IOCTL_INT_CONNECT:
      {
         nRetval = ioctl_int_connect(pDevDesc, arg);
         if (0 != nRetval)
         {
            ERR("ioctl ATEMSYS_IOCTL_INT_CONNECT failed: %d\n", nRetval);
            goto Exit;
         }
      } break;

      case ATEMSYS_IOCTL_INT_DISCONNECT:
      {
         nRetval = dev_int_disconnect(pDevDesc);
         if (0 != nRetval)
         {
            /* be quiet. ioctl may fail */
            goto Exit;
         }
      } break;

      case ATEMSYS_IOCTL_INT_INFO:
      {
         nRetval = ioctl_intinfo(pDevDesc, arg);
         if (0 != nRetval)
         {
            ERR("ioctl ATEMSYS_IOCTL_INT_INFO failed: %d\n", nRetval);
            goto Exit;
         }
      } break;

      case ATEMSYS_IOCTL_MOD_GETVERSION:
      {
         char aVersion[3] = {ATEMSYS_VERSION_NUM};
         __u32 dwVersion = ((aVersion[0] << 2 * 8) | (aVersion[1] << 1 * 8) | (aVersion[2] << 0 * 8));

#if (defined CONFIG_XENO_COBALT)
         nRetval = rtdm_safe_copy_to_user(fd, user_arg, &dwVersion, sizeof(__u32));
#else
         nRetval = put_user(dwVersion, (__u32*)arg);
#endif /* CONFIG_XENO_COBALT */

         if (0 != nRetval)
         {
            ERR("ioctl ATEMSYS_IOCTL_MOD_GETVERSION failed: %d\n", nRetval);
            goto Exit;
         }
      } break;

      case ATEMSYS_IOCTL_CPU_ENABLE_CYCLE_COUNT:
      {
#if (defined(__GNUC__) && (defined(__ARM__) || defined(__arm__) || defined(__aarch64__)))
         __u32 dwEnableUserMode = 0;

#if (defined CONFIG_XENO_COBALT)
         nRetval = rtdm_safe_copy_from_user(fd, &dwEnableUserMode, user_arg, sizeof(__u32));
#else
         nRetval = get_user(dwEnableUserMode, (__u32*)arg);
#endif
         if (0 != nRetval)
         {
            ERR("ioctl ATEMSYS_IOCTL_CPU_ENABLE_CYCLE_COUNT failed: %d\n", nRetval);
            goto Exit;
         }

         on_each_cpu(ioctl_enableCycleCount, &dwEnableUserMode, 1);
#else
         nRetval = -ENODEV;
         goto Exit;
#endif
      } break;

#if (defined INCLUDE_ATEMSYS_DT_DRIVER)
    case ATEMSYS_IOCTL_GET_MAC_INFO:
    {
        nRetval = GetMacInfoIoctl(pDevDesc, arg);
        if (0 != nRetval)
        {
            ERR("ioctl ATEMSYS_IOCTL_GET_MAC_INFO failed: 0x%x\n", nRetval);
            goto Exit;
        }
    } break;
    case ATEMSYS_IOCTL_PHY_START_STOP:
    {
        nRetval = PhyStartStopIoctl(arg);
        if (0 != nRetval)
        {
            ERR("ioctl ATEMSYS_IOCTL_PHY_START_STOP failed: %d\n", nRetval);
            goto Exit;
        }
    } break;
    case ATEMSYS_IOCTL_GET_MDIO_ORDER:
    {
        nRetval = GetMdioOrderIoctl(arg);
        if (0 != nRetval)
        {
            ERR("ioctl ATEMSYS_IOCTL_GET_MDIO_ORDER failed: %d\n", nRetval);
            goto Exit;
        }
    } break;
    case ATEMSYS_IOCTL_RETURN_MDIO_ORDER:
    {
        nRetval = ReturnMdioOrderIoctl(arg);
        if (0 != nRetval)
        {
            ERR("ioctl ATEMSYS_IOCTL_RETURN_MDIO_ORDER failed: %d\n", nRetval);
            goto Exit;
        }
    } break;
    case ATEMSYS_IOCTL_GET_PHY_INFO:
    {
        nRetval = GetPhyInfoIoctl(arg);
        if (0 != nRetval)
        {
            ERR("ioctl ATEMSYS_IOCTL_GET_PHY_INFO failed: %d\n", nRetval);
            goto Exit;
        }
      } break;
#endif /* INCLUDE_ATEMSYS_DT_DRIVER */

      default:
      {
         nRetval = -EOPNOTSUPP;
         goto Exit;
      } /* no break */
   }

   nRetval = DRIVER_SUCCESS;

Exit:
   return nRetval;
}

#if (defined CONFIG_COMPAT) && !(defined CONFIG_XENO_COBALT)
/*
 * ioctl processing for 32 bit process on 64 bit system
 */
static long atemsys_compat_ioctl(
      struct file *file,
      unsigned int cmd,
      unsigned long arg)
{
   return atemsys_ioctl(file, cmd, (unsigned long) compat_ptr(arg));
}
#endif /* CONFIG_COMPAT && !CONFIG_XENO_COBALT */

/* Module Declarations */

/*
 * This structure will hold the functions to be called
 * when a process does something to the device we
 * created. Since a pointer to this structure is kept in
 * the devices table, it can't be local to
 * module_init. NULL is for unimplemented functions.
 */

#if (defined CONFIG_XENO_COBALT)
static struct rtdm_driver driver = {
        .profile_info = RTDM_PROFILE_INFO(atemsys, RTDM_CLASS_EXPERIMENTAL, MAJOR_NUM, 1),
        .device_flags = RTDM_NAMED_DEVICE,
        .device_count = 1,
        .context_size = sizeof(dev_node),

        .ops = {
        .open = device_open,
        .close = device_release,
        .read_rt = device_read,
        .ioctl_rt = atemsys_ioctl,
        .ioctl_nrt = atemsys_ioctl,
        .mmap = device_mmap,
    },
};

static struct rtdm_device device = {
        .driver = &driver,
        .label = ATEMSYS_DEVICE_NAME,
};
#else /* !CONFIG_XENO_COBALT */
struct file_operations Fops = {
   .read = device_read,
   .unlocked_ioctl = atemsys_ioctl,
#if (defined CONFIG_COMPAT)
   .compat_ioctl = atemsys_compat_ioctl, /* ioctl processing for 32 bit process on 64 bit system */
#endif
   .open = device_open,
   .mmap = device_mmap,
   .release = device_release,   /* a.k.a. close */
};
#endif /* !CONFIG_XENO_COBALT */


#if (defined INCLUDE_ATEMSYS_DT_DRIVER)
static int GetMacInfoIoctl(dev_node* pDevDesc, unsigned long ioctlParam)
{
    ATEMSYS_T_MAC_INFO* pInfoUserSpace = (ATEMSYS_T_MAC_INFO *)ioctlParam;
    ATEMSYS_T_MAC_INFO Info;
    ATEMSYS_T_ETH_DRV_DESC_PRIVATE* pEthDrvDescPrivate  = NULL;
    unsigned int dwRetVal = 0;
    int nRetVal = -1;
    int nRes = -1;
    unsigned int i = 0;

    for (i = 0; i < EC_LINKOS_IDENT_MAX_LEN; i++)
    {
        nRes = get_user(Info.szIdent[i], &pInfoUserSpace->szIdent[i]);
        if (0 != nRes) { nRetVal = nRes; goto Exit; }
    }
    nRes = get_user(Info.dwInstance, &pInfoUserSpace->dwInstance);
    if (0 != nRes) { nRetVal = nRes; goto Exit; }

    for (i = 0; i < ATEMSYS_MAX_NUMBER_OF_ETHERNET_PORTS; i++)
    {
        if (NULL == S_apEthDrvDescPrivate[i])
        {
            continue;
        }
        if ((0 == strcmp(S_apEthDrvDescPrivate[i]->MacInfo.szIdent, Info.szIdent)) &&
            (S_apEthDrvDescPrivate[i]->MacInfo.dwInstance == Info.dwInstance))
        {
            pEthDrvDescPrivate = S_apEthDrvDescPrivate[i];
            break;
        }
    }

    if (NULL != pEthDrvDescPrivate)
    {
        nRes = put_user(pEthDrvDescPrivate->MacInfo.qwRegAddr,  &pInfoUserSpace->qwRegAddr);
        if (0 != nRes) { nRetVal = nRes; goto Exit; }

        nRes = put_user(pEthDrvDescPrivate->MacInfo.dwRegSize,  &pInfoUserSpace->dwRegSize);
        if (0 != nRes) { nRetVal = nRes; goto Exit; }

        nRes |= put_user(pEthDrvDescPrivate->MacInfo.dwStatus,  &pInfoUserSpace->dwStatus);
        if (0 != nRes) { nRetVal = nRes; goto Exit; }

        nRes |= put_user(pEthDrvDescPrivate->MacInfo.ePhyMode,  &pInfoUserSpace->ePhyMode);
        if (0 != nRes) { nRetVal = nRes; goto Exit; }

        nRes |= put_user(pEthDrvDescPrivate->MacInfo.dwIndex,   &pInfoUserSpace->dwIndex);
        if (0 != nRes) { nRetVal = nRes; goto Exit; }

        nRes |= put_user(pEthDrvDescPrivate->MacInfo.bNoMdioBus,&pInfoUserSpace->bNoMdioBus);
        if (0 != nRes) { nRetVal = nRes; goto Exit; }

        nRes |= put_user(pEthDrvDescPrivate->MacInfo.dwPhyAddr, &pInfoUserSpace->dwPhyAddr);
        if (0 != nRes) { nRetVal = nRes; goto Exit; }

        /* remember descriptor of callee for cleanup on device_release */
        pEthDrvDescPrivate->pDevDesc = pDevDesc;
        dwRetVal = 0; /* EC_E_NOERROR */
    }
    else
    {
        dwRetVal = 0x9811000C; /* EC_E_NOTFOUND */
    }
    nRetVal = 0;

Exit:
    if (0 == nRetVal)
    {
        put_user(dwRetVal ,&pInfoUserSpace->dwErrorCode);
    }
    else
    {
        put_user(0x98110000 /* EC_E_ERROR */ ,&pInfoUserSpace->dwErrorCode);
    }

    return nRetVal;
}

static int PhyStartStopIoctl( unsigned long ioctlParam)
{
    ATEMSYS_T_PHY_START_STOP_INFO* pPhyStartStopInfoUserSpace = (ATEMSYS_T_PHY_START_STOP_INFO *)ioctlParam;
    ATEMSYS_T_PHY_START_STOP_INFO PhyStartStopInfo;
    ATEMSYS_T_ETH_DRV_DESC_PRIVATE* pEthDrvDescPrivate = NULL;
    unsigned int dwRetVal = 0;
    int nRetVal = -1;
    int nRes = -1;


    nRes =  get_user(PhyStartStopInfo.dwIndex, &pPhyStartStopInfoUserSpace->dwIndex);
    if (0 != nRes) { nRetVal = nRes; goto Exit; }

    nRes = get_user(PhyStartStopInfo.bStart,  &pPhyStartStopInfoUserSpace->bStart);
    if (0 != nRes) { nRetVal = nRes; goto Exit; }

    if ((PhyStartStopInfo.dwIndex >= ATEMSYS_MAX_NUMBER_OF_ETHERNET_PORTS))
    {
        PhyStartStopInfo.dwErrorCode = 0x98110002; /* EC_E_INVALIDINDEX */
        nRetVal = 0;
        goto Exit;
    }
    pEthDrvDescPrivate = S_apEthDrvDescPrivate[PhyStartStopInfo.dwIndex];
    if (NULL == S_apEthDrvDescPrivate[PhyStartStopInfo.dwIndex])
    {
        dwRetVal = 0x9811000C; /* EC_E_NOTFOUND*/
        nRetVal = 0;
        goto Exit;
    }

    if (PhyStartStopInfo.bStart)
    {
        pEthDrvDescPrivate->etx_thread_StartPhy = kthread_create(StartPhyThread,(void*)pEthDrvDescPrivate->pPDev,"StartPhyThread");
        if(NULL == pEthDrvDescPrivate->etx_thread_StartPhy)
        {
            ERR("Cannot create kthread for StartPhyThread\n");
            nRetVal = -EAGAIN;
            goto Exit;
        }
        wake_up_process(pEthDrvDescPrivate->etx_thread_StartPhy);
        dwRetVal = 0; /* EC_E_NOERROR */
    }
    else
    {
        pEthDrvDescPrivate->etx_thread_StopPhy = kthread_create(StopPhyThread,(void*)pEthDrvDescPrivate->pPDev,"StopPhyThread");
        if(NULL == pEthDrvDescPrivate->etx_thread_StopPhy)
        {
            ERR("Cannot create kthread for StopPhyThread\n");
            nRetVal = -EAGAIN;
            goto Exit;
        }
        wake_up_process(pEthDrvDescPrivate->etx_thread_StopPhy);
        dwRetVal = 0; /* EC_E_NOERROR */
    }

    nRetVal = 0;
Exit:
    if (0 == nRetVal)
    {
        put_user(dwRetVal, &pPhyStartStopInfoUserSpace->dwErrorCode);
    }
    else
    {
        put_user(0x98110000 /* EC_E_ERROR */, &pPhyStartStopInfoUserSpace->dwErrorCode);
    }

    return nRetVal;
}


static int GetMdioOrderIoctl( unsigned long ioctlParam)
{
    ATEMSYS_T_ETH_DRV_DESC_PRIVATE* pEthDrvDescPrivate = NULL;
    ATEMSYS_T_MDIO_ORDER* pOrderUserSpace = (ATEMSYS_T_MDIO_ORDER*)ioctlParam;
    unsigned int dwIndex = 0;
    bool bLocked = false;
    unsigned int dwRetVal = 0;
    int nRetVal = -1;
    int nRes = -1;


    nRes = get_user(dwIndex, &pOrderUserSpace->dwIndex);
    if (0 != nRes) { nRetVal = nRes; goto Exit; }

    if (dwIndex >= ATEMSYS_MAX_NUMBER_OF_ETHERNET_PORTS)
    {
        dwRetVal = 0x98110002; /* EC_E_INVALIDINDEX */
        nRetVal = 0;
        goto Exit;
    }
    pEthDrvDescPrivate = S_apEthDrvDescPrivate[dwIndex];
    if (NULL == pEthDrvDescPrivate)
    {
        dwRetVal = 0x9811000C; /* EC_E_NOTFOUND*/
        nRetVal = 0;
        goto Exit;
    }

    if (mutex_trylock(&pEthDrvDescPrivate->mdio_order_mutex))
    {
        bLocked = true;
        if ((pEthDrvDescPrivate->MdioOrder.bInUse) && (pEthDrvDescPrivate->MdioOrder.bInUseByIoctl))
        {
            nRes = put_user(pEthDrvDescPrivate->MdioOrder.bInUse, &pOrderUserSpace->bInUse);
            if (0 != nRes) { nRetVal = nRes; goto Exit; }

            nRes = put_user(pEthDrvDescPrivate->MdioOrder.bInUseByIoctl,&pOrderUserSpace->bInUseByIoctl);
            if (0 != nRes) { nRetVal = nRes; goto Exit; }

            nRes = put_user(pEthDrvDescPrivate->MdioOrder.bWriteOrder, &pOrderUserSpace->bWriteOrder);
            if (0 != nRes) { nRetVal = nRes; goto Exit; }

            nRes = put_user(pEthDrvDescPrivate->MdioOrder.wMdioAddr, &pOrderUserSpace->wMdioAddr);
            if (0 != nRes) { nRetVal = nRes; goto Exit; }

            nRes = put_user(pEthDrvDescPrivate->MdioOrder.wReg, &pOrderUserSpace->wReg);
            if (0 != nRes) { nRetVal = nRes; goto Exit; }

            nRes = put_user(pEthDrvDescPrivate->MdioOrder.wValue, &pOrderUserSpace->wValue);
            if (0 != nRes) { nRetVal = nRes; goto Exit; }
        }
    }
    dwRetVal = 0; /* EC_E_NOERROR*/
    nRetVal = 0;

Exit:
    if (bLocked)
    {
        mutex_unlock(&pEthDrvDescPrivate->mdio_order_mutex);
    }
    if (0 == nRetVal)
    {
        put_user(dwRetVal, &pOrderUserSpace->dwErrorCode);
    }
    else
    {
        put_user(0x98110000 /* EC_E_ERROR */, &pOrderUserSpace->dwErrorCode);
    }

    return nRetVal;
}

static int ReturnMdioOrderIoctl( unsigned long ioctlParam)
{
    ATEMSYS_T_ETH_DRV_DESC_PRIVATE* pEthDrvDescPrivate = NULL;
    ATEMSYS_T_MDIO_ORDER* pOrderUserSpace = (ATEMSYS_T_MDIO_ORDER*)ioctlParam;
    unsigned int dwIndex = 0;
    __u16 wValue = 0;
    unsigned int dwRetVal = 0;
    int nRetVal = -1;
    int nRes = -1;

    nRes = get_user(dwIndex, &pOrderUserSpace->dwIndex);
    if (0 != nRes) { nRetVal = nRes; goto Exit; }

    if (dwIndex >= ATEMSYS_MAX_NUMBER_OF_ETHERNET_PORTS)
    {
        dwRetVal =  0x98110002; /* EC_E_INVALIDINDEX */
        nRetVal = 0;
        goto Exit;
    }
    pEthDrvDescPrivate = S_apEthDrvDescPrivate[dwIndex];
    if (NULL == pEthDrvDescPrivate)
    {
        dwRetVal = 0x9811000C; /* EC_E_NOTFOUND*/
        nRetVal = 0;
        goto Exit;
    }

    nRes = get_user(wValue, &pOrderUserSpace->wValue);
    if (0 != nRes) { nRetVal = nRes; goto Exit; }

    mutex_lock(&pEthDrvDescPrivate->mdio_order_mutex);
    pEthDrvDescPrivate->MdioOrder.wValue = wValue;
    pEthDrvDescPrivate->MdioOrder.bInUseByIoctl = false;
    mutex_unlock(&pEthDrvDescPrivate->mdio_order_mutex);

    /* wake MdioRead or MdioWrite */
    pEthDrvDescPrivate->mdio_wait_queue_flag = 1;
    wake_up_interruptible(&pEthDrvDescPrivate->mdio_wait_queue);

    dwRetVal = 0 /* EC_E_NOERROR*/;
    nRetVal = 0;

Exit:
    if (0 == nRetVal)
    {
        put_user(dwRetVal, &pOrderUserSpace->dwErrorCode);
    }
    else
    {
        put_user(0x98110000 /* EC_E_ERROR */, &pOrderUserSpace->dwErrorCode);
    }

    return nRetVal;
}

static int GetPhyInfoIoctl(unsigned long ioctlParam)
{
    ATEMSYS_T_ETH_DRV_DESC_PRIVATE* pEthDrvDescPrivate  = NULL;
    ATEMSYS_T_PHY_INFO* pStatusUserSpace = (ATEMSYS_T_PHY_INFO *)ioctlParam;
    unsigned int dwIndex = 0;
    unsigned int dwRetVal = 0;
    int nRetVal = -1;
    int nRes = -1;

    nRes = get_user(dwIndex, &pStatusUserSpace->dwIndex);
    if (0 != nRes) { nRetVal = nRes; goto Exit; }

    if (dwIndex >= ATEMSYS_MAX_NUMBER_OF_ETHERNET_PORTS)
    {
        dwRetVal = 0x98110002; /* EC_E_INVALIDINDEX */
        nRetVal = 0;
        goto Exit;
    }
    pEthDrvDescPrivate = S_apEthDrvDescPrivate[dwIndex];
    if (NULL == pEthDrvDescPrivate)
    {
        dwRetVal = 0x9811000C; /* EC_E_NOTFOUND*/
        nRetVal = 0;
        goto Exit;
    }

    nRes = put_user(pEthDrvDescPrivate->PhyInfo.dwLink, &pStatusUserSpace->dwLink);
    if (0 != nRes) { nRetVal = nRes; goto Exit; }

    nRes = put_user(pEthDrvDescPrivate->PhyInfo.dwDuplex, &pStatusUserSpace->dwDuplex);
    if (0 != nRes) { nRetVal = nRes; goto Exit; }

    nRes = put_user(pEthDrvDescPrivate->PhyInfo.dwSpeed, &pStatusUserSpace->dwSpeed);
    if (0 != nRes) { nRetVal = nRes; goto Exit; }

    nRes = put_user(pEthDrvDescPrivate->PhyInfo.bPhyReady, &pStatusUserSpace->bPhyReady);
    if (0 != nRes) { nRetVal = nRes; goto Exit; }

    dwRetVal = 0; /* EC_E_NOERROR */
    nRetVal = 0;

Exit:
    if (0 == nRetVal)
    {
        put_user(dwRetVal, &pStatusUserSpace->dwErrorCode);
    }
    else
    {
        put_user(0x98110000 /* EC_E_ERROR */, &pStatusUserSpace->dwErrorCode);
    }

    return nRetVal;
}

static void UpdatePhyInfoByLinuxPhyDriver(struct net_device *ndev)
{
    struct phy_device* phy_dev = ndev->phydev;
    ATEMSYS_T_ETH_DRV_DESC_PRIVATE* pEthDrvDescPrivate = netdev_priv(ndev);

    pEthDrvDescPrivate->PhyInfo.dwLink = phy_dev->link;
    pEthDrvDescPrivate->PhyInfo.dwDuplex = phy_dev->duplex;
    pEthDrvDescPrivate->PhyInfo.dwSpeed = phy_dev->speed;
    pEthDrvDescPrivate->PhyInfo.bPhyReady = true;
}

static int MdioProbe(struct net_device *ndev)
{
    ATEMSYS_T_ETH_DRV_DESC_PRIVATE* pEthDrvDescPrivate = netdev_priv(ndev);
    struct phy_device* pPhyDev = NULL;
    char mdio_bus_id[MII_BUS_ID_SIZE];
    char phy_name[MII_BUS_ID_SIZE + 3];
    int nPhy_id = 0;

    if (NULL != pEthDrvDescPrivate->pPhyNode)
    {
        pPhyDev = of_phy_connect(ndev, pEthDrvDescPrivate->pPhyNode,
                     &UpdatePhyInfoByLinuxPhyDriver, 0,
                     pEthDrvDescPrivate->PhyInterface);
    }
    else if (NULL != pEthDrvDescPrivate->pMdioBus)
    {
        int nDev_id = pEthDrvDescPrivate->nDev_id;
        /* check for attached phy */
        for (nPhy_id = 0; (nPhy_id < PHY_MAX_ADDR); nPhy_id++)
        {
            if (!mdiobus_is_registered_device(pEthDrvDescPrivate->pMdioBus, nPhy_id))
            {
                continue;
            }
            if (0 != nDev_id--)
            {
                continue;
            }
            strlcpy(mdio_bus_id, pEthDrvDescPrivate->pMdioBus->id, MII_BUS_ID_SIZE);
            break;
        }

        if (nPhy_id >= PHY_MAX_ADDR)
        {
            INF("%s: no PHY, assuming direct connection to switch\n", pEthDrvDescPrivate->pPDev->name);
            strlcpy(mdio_bus_id, "fixed-0", MII_BUS_ID_SIZE);
            nPhy_id = 0;
        }

        snprintf(phy_name, sizeof(phy_name), PHY_ID_FMT, mdio_bus_id, nPhy_id);
        pPhyDev = phy_connect(ndev, phy_name, &UpdatePhyInfoByLinuxPhyDriver, pEthDrvDescPrivate->PhyInterface);
    }

    if ((NULL == pPhyDev) || IS_ERR(pPhyDev))
    {
        ERR("%s: Could not attach to PHY (pPhyDev %p)\n", pEthDrvDescPrivate->pPDev->name, pPhyDev);
        return -ENODEV;
    }

    /* adjust maximal link speed */
#if (LINUX_VERSION_CODE >= KERNEL_VERSION(5,0,0))
    phy_set_max_speed(pPhyDev, 100);
#else
    pPhyDev->supported &= PHY_BASIC_FEATURES;
    pPhyDev->advertising = pPhyDev->supported;
#endif
    if (LOGLEVEL_INFO <= loglevel)
    {
        phy_attached_info(pPhyDev);
    }

    pEthDrvDescPrivate->pPhyDev = pPhyDev;
    pEthDrvDescPrivate->PhyInfo.dwLink = 0;
    pEthDrvDescPrivate->PhyInfo.dwDuplex = 0;
    pEthDrvDescPrivate->PhyInfo.dwSpeed = 0;

    return 0;
}

static int MdioRead(struct mii_bus *pBus, int mii_id, int regnum)
{
    ATEMSYS_T_ETH_DRV_DESC_PRIVATE* pEthDrvDescPrivate = pBus->priv;
    int nRetVal = -1;
    int nRes = -1;

    nRes = pm_runtime_get_sync(&pEthDrvDescPrivate->pPDev->dev);
    if (0 > nRes)
    {
        return nRes;
    }

    /* get lock for the Mdio bus only one MdioRead or MdioWrite*/
    mutex_lock(&pEthDrvDescPrivate->mdio_mutex);

    mutex_lock(&pEthDrvDescPrivate->mdio_order_mutex);
    memset(&pEthDrvDescPrivate->MdioOrder, 0, sizeof(ATEMSYS_T_MDIO_ORDER));
    pEthDrvDescPrivate->MdioOrder.bInUse = true;
    pEthDrvDescPrivate->MdioOrder.bInUseByIoctl = true;
    pEthDrvDescPrivate->MdioOrder.bWriteOrder = false;
    pEthDrvDescPrivate->MdioOrder.wMdioAddr = (__u16)mii_id;
    pEthDrvDescPrivate->MdioOrder.wReg = (__u16)regnum;
    mutex_unlock(&pEthDrvDescPrivate->mdio_order_mutex);

    /* wait for result */
    wait_event_interruptible_timeout(pEthDrvDescPrivate->mdio_wait_queue, pEthDrvDescPrivate->mdio_wait_queue_flag != 0, HZ /* at least 1 second */);
    if (pEthDrvDescPrivate->mdio_wait_queue_flag == 0)
    {
        nRetVal = -ETIMEDOUT;
        goto Exit;
    }
    pEthDrvDescPrivate->mdio_wait_queue_flag = 0;

    /* finished (not time-out) */
    nRetVal = pEthDrvDescPrivate->MdioOrder.wValue;

Exit:
    mutex_lock(&pEthDrvDescPrivate->mdio_order_mutex);
    pEthDrvDescPrivate->MdioOrder.bInUse = false;
    pEthDrvDescPrivate->MdioOrder.bInUseByIoctl = false;
    mutex_unlock(&pEthDrvDescPrivate->mdio_order_mutex);

    pm_runtime_mark_last_busy(&pEthDrvDescPrivate->pPDev->dev);
    pm_runtime_put_autosuspend(&pEthDrvDescPrivate->pPDev->dev);

    mutex_unlock(&pEthDrvDescPrivate->mdio_mutex);

    return nRetVal;
}

static int MdioWrite(struct mii_bus *pBus, int mii_id, int regnum, u16 value)
{
    ATEMSYS_T_ETH_DRV_DESC_PRIVATE* pEthDrvDescPrivate = pBus->priv;
    int nRetVal;

    nRetVal = pm_runtime_get_sync(&pEthDrvDescPrivate->pPDev->dev);
    if (0 > nRetVal)
    {
        return nRetVal;
    }

    /* get lock for the Mdio bus only one MdioRead or MdioWrite*/
    mutex_lock(&pEthDrvDescPrivate->mdio_mutex);

    mutex_lock(&pEthDrvDescPrivate->mdio_order_mutex);
    memset(&pEthDrvDescPrivate->MdioOrder, 0, sizeof(ATEMSYS_T_MDIO_ORDER));
    pEthDrvDescPrivate->MdioOrder.bInUse = true;
    pEthDrvDescPrivate->MdioOrder.bInUseByIoctl = true;
    pEthDrvDescPrivate->MdioOrder.bWriteOrder = true;
    pEthDrvDescPrivate->MdioOrder.wMdioAddr = (__u16)mii_id;
    pEthDrvDescPrivate->MdioOrder.wReg = (__u16)regnum;
    pEthDrvDescPrivate->MdioOrder.wValue = (__u16)value;
    mutex_unlock(&pEthDrvDescPrivate->mdio_order_mutex);

    /* wait for result */
    wait_event_interruptible_timeout(pEthDrvDescPrivate->mdio_wait_queue, pEthDrvDescPrivate->mdio_wait_queue_flag != 0, HZ /* at least 1 second */);
    if (pEthDrvDescPrivate->mdio_wait_queue_flag == 0)
    {
        nRetVal = -ETIMEDOUT;
        goto Exit;
    }
    pEthDrvDescPrivate->mdio_wait_queue_flag = 0;

    /* finished (not time-out) */
    nRetVal = 0;

Exit:
    mutex_lock(&pEthDrvDescPrivate->mdio_order_mutex);
    pEthDrvDescPrivate->MdioOrder.bInUse = false;
    pEthDrvDescPrivate->MdioOrder.bInUseByIoctl = false;
    mutex_unlock(&pEthDrvDescPrivate->mdio_order_mutex);

    pm_runtime_mark_last_busy(&pEthDrvDescPrivate->pPDev->dev);
    pm_runtime_put_autosuspend(&pEthDrvDescPrivate->pPDev->dev);

    mutex_unlock(&pEthDrvDescPrivate->mdio_mutex);

    return nRetVal;
}

static int MdioInit(struct platform_device *pPDev)
{
    struct net_device* pNDev = platform_get_drvdata(pPDev);
    ATEMSYS_T_ETH_DRV_DESC_PRIVATE* pEthDrvDescPrivate = netdev_priv(pNDev);
    struct device_node* pDevNode;
    int nRes = -ENXIO;

    if (pEthDrvDescPrivate->MacInfo.bNoMdioBus)
    {
        pEthDrvDescPrivate->pMdioBus = NULL;
        nRes = 0;
        goto Exit;
    }

    pEthDrvDescPrivate->pMdioBus = mdiobus_alloc();
    if (NULL == pEthDrvDescPrivate->pMdioBus)
    {
        nRes = -ENOMEM;
        goto Exit;
    }

    pEthDrvDescPrivate->pMdioBus->name = "atemsys_mdio_bus";
    pEthDrvDescPrivate->pMdioBus->read = &MdioRead;
    pEthDrvDescPrivate->pMdioBus->write = &MdioWrite;
    snprintf(pEthDrvDescPrivate->pMdioBus->id, MII_BUS_ID_SIZE, "%s-%x", pPDev->name, pEthDrvDescPrivate->nDev_id + 1);
    pEthDrvDescPrivate->pMdioBus->priv = pEthDrvDescPrivate;
    pEthDrvDescPrivate->pMdioBus->parent = &pPDev->dev;

    pDevNode = of_get_child_by_name(pPDev->dev.of_node, "mdio");
    if (NULL != pDevNode)
    {
        nRes = of_mdiobus_register(pEthDrvDescPrivate->pMdioBus, pDevNode);
        of_node_put(pDevNode);
    }
    else
    {
        nRes = mdiobus_register(pEthDrvDescPrivate->pMdioBus);
    }
    if (0 != nRes)
    {
        mdiobus_free(pEthDrvDescPrivate->pMdioBus);
    }

Exit:
    return nRes;
}


static int StopPhy(struct platform_device *pPDev)
{
    struct net_device* pNDev = platform_get_drvdata(pPDev);
    ATEMSYS_T_ETH_DRV_DESC_PRIVATE* pEthDrvDescPrivate = netdev_priv(pNDev);

    /* phy */
    if (NULL != pEthDrvDescPrivate->pPhyDev)
    {
        phy_stop(pEthDrvDescPrivate->pPhyDev);
        phy_disconnect(pEthDrvDescPrivate->pPhyDev);
        pEthDrvDescPrivate->pPhyDev = NULL;
    }

    /* mdio bus */
    if (NULL != pEthDrvDescPrivate->pMdioBus)
    {
        mdiobus_unregister(pEthDrvDescPrivate->pMdioBus);
        mdiobus_free(pEthDrvDescPrivate->pMdioBus);
        pEthDrvDescPrivate->pMdioBus = NULL;
    }

    pEthDrvDescPrivate->PhyInfo.bPhyReady = false;

    return 0;
}

static int StartPhy(struct platform_device *pPDev)
{
    struct net_device* pNDev = platform_get_drvdata(pPDev);
    ATEMSYS_T_ETH_DRV_DESC_PRIVATE* pEthDrvDescPrivate = netdev_priv(pNDev);
    int nRes = -1;

    if ((NULL != pEthDrvDescPrivate->pPhyDev) || (NULL != pEthDrvDescPrivate->pMdioBus))
    {
        StopPhy(pPDev);
    }

    /* mdio bus */
    nRes = MdioInit(pPDev);
    if (0 != nRes)
    {
        pEthDrvDescPrivate->pMdioBus = NULL;
    }
    nRes = MdioProbe(pNDev);
    if (0 != nRes)
    {
        return nRes;
    }
    /* phy */
    phy_start(pEthDrvDescPrivate->pPhyDev);
    phy_start_aneg(pEthDrvDescPrivate->pPhyDev);

    return 0;
}

static int StartPhyThread(void *data)
{
    struct platform_device *pPDev = (struct platform_device *)data;

    StartPhy(pPDev);

    return 0;
}

static int StopPhyThread(void *data)
{
    struct platform_device *pPDev = (struct platform_device *)data;

    StopPhy(pPDev);

    return 0;
}

static int EthernetDriverProbe(struct platform_device *pPDev)
{
    ATEMSYS_T_ETH_DRV_DESC_PRIVATE* pEthDrvDescPrivate = NULL;
    struct net_device* pNDev = NULL;
    const struct of_device_id* pOf_id = NULL;
    static int nDev_id = 0;
    unsigned int dwIndex = 0;
    int nRes = 0;
    struct device_node* pDevNode = NULL;

    INF("Atemsys: Probe device: %s\n", pPDev->name);

    pDevNode = pPDev->dev.of_node;
    if (NULL == pDevNode)
    {
        ERR("%s: Device node not found\n", pPDev->name);
        return -ENODATA;
    }

    /* Init network device */
    pNDev = alloc_etherdev_mqs(sizeof(ATEMSYS_T_ETH_DRV_DESC_PRIVATE), 1 , 1); /* No TX and RX queues requiered */
    if (NULL == pNDev)
    {
        return -ENOMEM;
    }
    SET_NETDEV_DEV(pNDev, &pPDev->dev);

    /* setup board info structure */
    pOf_id = of_match_device(atemsys_ids, &pPDev->dev);
    if (NULL != pOf_id)
    {
        pPDev->id_entry = pOf_id->data;
    }

    pEthDrvDescPrivate = netdev_priv(pNDev);
    memset(pEthDrvDescPrivate, 0, sizeof(ATEMSYS_T_ETH_DRV_DESC_PRIVATE));
    pEthDrvDescPrivate->pPDev = pPDev;
    pEthDrvDescPrivate->nDev_id  = nDev_id++;
    platform_set_drvdata(pPDev, pNDev);
    pEthDrvDescPrivate->netdev = pNDev;
    pEthDrvDescPrivate->pDevNode = pDevNode;

    /* Select default pin state */
    pinctrl_pm_select_default_state(&pPDev->dev);

    /* enable clock */
    pEthDrvDescPrivate->nCountClk = of_property_count_strings(pDevNode,"clock-names");
    if (0 > pEthDrvDescPrivate->nCountClk)
    {
        pEthDrvDescPrivate->nCountClk = 0;
    }
    DBG("%s: found %d Clocks\n", pPDev->name , pEthDrvDescPrivate->nCountClk);

    for (dwIndex = 0; dwIndex < pEthDrvDescPrivate->nCountClk; dwIndex++)
    {
        if(!of_property_read_string_index(pDevNode, "clock-names", dwIndex, &pEthDrvDescPrivate->clk_ids[dwIndex]))
        {
            pEthDrvDescPrivate->clks[dwIndex] = devm_clk_get(&pPDev->dev, pEthDrvDescPrivate->clk_ids[dwIndex]);
            if (!IS_ERR(pEthDrvDescPrivate->clks[dwIndex]))
            {
                clk_prepare_enable(pEthDrvDescPrivate->clks[dwIndex]);
                DBG("%s: Clock %s enabled\n", pPDev->name, pEthDrvDescPrivate->clk_ids[dwIndex]);
            }
            else
            {
                pEthDrvDescPrivate->clks[dwIndex] = NULL;
            }
        }
    }

    /* enable PHY regulator*/
    pEthDrvDescPrivate->pPhyRegulator = devm_regulator_get(&pPDev->dev, "phy");
    if (!IS_ERR(pEthDrvDescPrivate->pPhyRegulator))
    {
        if (regulator_enable(pEthDrvDescPrivate->pPhyRegulator))
        {
            WRN("%s: can't enable PHY regulator!\n", pPDev->name);
        }
    }
    else
    {
        pEthDrvDescPrivate->pPhyRegulator = NULL;
    }

    /* Device run-time power management */
    pm_runtime_dont_use_autosuspend(&pPDev->dev);
    pm_runtime_get_noresume(&pPDev->dev);
    pm_runtime_set_active(&pPDev->dev);
    pm_runtime_enable(&pPDev->dev);

    /* get prepare data for atemsys and print some data to kernel log */
    {
        unsigned int    dwTemp          = 0;
        const char     *szTempString    = NULL;
        unsigned int    adwTempValues[6];

        /* get identification */
        nRes = of_property_read_string(pDevNode, "atemsys-Ident", &szTempString);
        if ((0 == nRes) && (NULL != szTempString))
        {
            INF("%s: atemsys-Ident: %s\n", pPDev->name, szTempString);
            memcpy(pEthDrvDescPrivate->MacInfo.szIdent,szTempString, EC_LINKOS_IDENT_MAX_LEN);
        }
        else
        {
            INF("%s: Missing atemsys-Ident in the Device Tree\n", pPDev->name);
        }

        /* get instance number */
        nRes = of_property_read_u32(pDevNode, "atemsys-Instance", &dwTemp);
        if (0 == nRes)
        {
            INF("%s: atemsys-Instance: %d\n", pPDev->name , dwTemp);
            pEthDrvDescPrivate->MacInfo.dwInstance = dwTemp;
        }
        else
        {
            pEthDrvDescPrivate->MacInfo.dwInstance = 0;
            INF("%s: Missing atemsys-Instance in the Device Tree\n", pPDev->name);
        }

        /* status */
        szTempString = NULL;
        nRes = of_property_read_string(pDevNode, "status", &szTempString);
        if ((0 == nRes) && (NULL != szTempString))
        {
            DBG("%s: status: %s\n", pPDev->name , szTempString);
            pEthDrvDescPrivate->MacInfo.dwStatus = (strcmp(szTempString, "okay")==0)? 1:0;
        }

        /* interrupt-parent */
        nRes = of_property_read_u32(pDevNode, "interrupt-parent", &dwTemp);
        if (0 == nRes)
        {
            DBG("%s: interrupt-parent: %d\n", pPDev->name , dwTemp);
        }

        /* interrupts */
        nRes = of_property_read_u32_array(pDevNode, "interrupts", adwTempValues, 6);
        if (0 == nRes)
        {
            DBG("%s: interrupts: 0x%x 0x%x 0x%x 0x%x 0x%x 0x%x\n", pPDev->name ,
                adwTempValues[0], adwTempValues[1], adwTempValues[2], adwTempValues[3], adwTempValues[4], adwTempValues[5]);
        }

        /* reg */
#if (defined __arm__)
        nRes = of_property_read_u32_array(pDevNode, "reg", adwTempValues, 2);
        if (0 == nRes)
        {
            DBG("%s: reg: 0x%x 0x%x\n", pPDev->name , adwTempValues[0], adwTempValues[1]);
            pEthDrvDescPrivate->MacInfo.qwRegAddr = adwTempValues[0];
            pEthDrvDescPrivate->MacInfo.dwRegSize = adwTempValues[1];
        }
#endif

        /* get phy-mode */
        szTempString = NULL;
        pEthDrvDescPrivate->MacInfo.ePhyMode = eATEMSYS_PHY_FIXED_LINK;
        nRes = of_property_read_string(pDevNode, "phy-mode", &szTempString);
        if ((0 == nRes) && (NULL != szTempString))
        {
            INF("%s: phy-mode: %s\n", pPDev->name , szTempString);

            if (strcmp(szTempString, "mii") == 0)
            {
                pEthDrvDescPrivate->MacInfo.ePhyMode = eATEMSYS_PHY_MII; /* for EcMaster */
                pEthDrvDescPrivate->PhyInterface = PHY_INTERFACE_MODE_MII; /* for Linux PHY Driver */
            }

            if (strcmp(szTempString, "rmii") == 0)
            {
                pEthDrvDescPrivate->MacInfo.ePhyMode = eATEMSYS_PHY_RMII;
                pEthDrvDescPrivate->PhyInterface = PHY_INTERFACE_MODE_RMII;
            }

            if (strcmp(szTempString, "gmii") == 0)
            {
                pEthDrvDescPrivate->MacInfo.ePhyMode = eATEMSYS_PHY_GMII;
                pEthDrvDescPrivate->PhyInterface = PHY_INTERFACE_MODE_GMII;
            }

            if (strcmp(szTempString, "sgmii") == 0)
            {
                pEthDrvDescPrivate->MacInfo.ePhyMode = eATEMSYS_PHY_SGMII;
                pEthDrvDescPrivate->PhyInterface = PHY_INTERFACE_MODE_SGMII;
            }

            if (strcmp(szTempString, "rgmii") == 0)
            {
                pEthDrvDescPrivate->MacInfo.ePhyMode = eATEMSYS_PHY_RGMII;
                pEthDrvDescPrivate->PhyInterface = PHY_INTERFACE_MODE_RGMII;
            }
        }
        else
        {
            pEthDrvDescPrivate->MacInfo.ePhyMode = eATEMSYS_PHY_MII; /* default */
            pEthDrvDescPrivate->PhyInterface = PHY_INTERFACE_MODE_MII;
            WRN("%s: Missing phy-mode in the Device Tree\n", pPDev->name);
        }

        /* pinctrl-names */
        szTempString = NULL;
        nRes = of_property_read_string(pDevNode, "pinctrl-names", &szTempString);
        if ((0 == nRes) && (NULL != szTempString))
        {
            DBG("%s: pinctrl-names: %s\n", pPDev->name , szTempString);
        }

        /* PHY address*/
        pEthDrvDescPrivate->MacInfo.dwPhyAddr = PHY_AUTO_ADDR;
        pEthDrvDescPrivate->pPhyNode = of_parse_phandle(pDevNode, "phy-handle", 0);
        if (NULL != pEthDrvDescPrivate->pPhyNode)
        {
            nRes = of_property_read_u32(pEthDrvDescPrivate->pPhyNode, "reg", &dwTemp);
            if (0 == nRes)
            {
                INF("%s: PHY mdio addr: %d\n", pPDev->name , dwTemp);
                pEthDrvDescPrivate->MacInfo.dwPhyAddr = dwTemp;
            }
        }
        else
        {
            INF("%s: Missing phy-handle in the Device Tree\n", pPDev->name);
        }

        /* look for mdio node */
        if (NULL == of_get_child_by_name(pPDev->dev.of_node, "mdio"))
        {
            if (NULL != pEthDrvDescPrivate->pPhyNode)
            {
                /* mdio bus owned by another mac instance */
                pEthDrvDescPrivate->MacInfo.bNoMdioBus = true;
                INF("%s: mac has no mdio bus, uses mdio bus of other instance.\n", pPDev->name );
            }
            else
            {
                /* legacy mode: no node for mdio bus in device tree defined */
                pEthDrvDescPrivate->MacInfo.bNoMdioBus = false;
                INF("%s: handle mdio bus without device tree node.\n", pPDev->name );
            }
        }
        else
        {
            /* mdio bus is owned by current mac instance */
            pEthDrvDescPrivate->MacInfo.bNoMdioBus = false;
            DBG("%s: mac has mdio bus.\n", pPDev->name );
        }
    }

    /* insert device to array */
    for (dwIndex = 0; dwIndex < ATEMSYS_MAX_NUMBER_OF_ETHERNET_PORTS; dwIndex++)
    {
        if (NULL == S_apEthDrvDescPrivate[dwIndex])
        {
            S_apEthDrvDescPrivate[dwIndex] = pEthDrvDescPrivate;
            pEthDrvDescPrivate->MacInfo.dwIndex =  dwIndex;
            break;
        }
    }
    if (dwIndex >= ATEMSYS_MAX_NUMBER_OF_ETHERNET_PORTS)
    {
        ERR("%s: Maximum number of instances exceeded!\n", pPDev->name);
        return EthernetDriverRemove(pPDev);
    }

    /* start drivers of sub-nodes */
    if (strcmp(pEthDrvDescPrivate->MacInfo.szIdent, "CPSW") == 0 
       || strcmp(pEthDrvDescPrivate->MacInfo.szIdent, "ICSS") == 0)
    {
        of_platform_populate(pDevNode, NULL, NULL, &pPDev->dev);
        DBG("%s: start drivers of sub-nodes.\n", pPDev->name );
    }

    /* prepare mutex for mdio */
    mutex_init(&pEthDrvDescPrivate->mdio_mutex);
    mutex_init(&pEthDrvDescPrivate->mdio_order_mutex);
    init_waitqueue_head(&pEthDrvDescPrivate->mdio_wait_queue);
    pEthDrvDescPrivate->mdio_wait_queue_flag = 0;

    return 0;
}


static int EthernetDriverRemove(struct platform_device *pPDev)
{
    struct net_device* pNDev = platform_get_drvdata(pPDev);
    ATEMSYS_T_ETH_DRV_DESC_PRIVATE* pEthDrvDescPrivate = netdev_priv(pNDev);
    unsigned int i = 0;

    if ((pEthDrvDescPrivate->pPhyDev != NULL) || (pEthDrvDescPrivate->pMdioBus != NULL))
    {
        StopPhy(pPDev);
    }

    if (NULL != pEthDrvDescPrivate->pPhyRegulator)
    {
        regulator_disable(pEthDrvDescPrivate->pPhyRegulator);
    }

    /* Decrement refcount */
    of_node_put(pEthDrvDescPrivate->pPhyNode);

    pm_runtime_put(&pPDev->dev);
    pm_runtime_disable(&pPDev->dev);

    for (i = 0; i < ATEMSYS_MAX_NUMBER_OF_CLOCKS; i++)
    {
        if (NULL != pEthDrvDescPrivate->clk_ids[i])
        {
            clk_disable_unprepare(pEthDrvDescPrivate->clks[i]);
            DBG("%s: Clock %s unprepared\n", pPDev->name, pEthDrvDescPrivate->clk_ids[i]);
        }
    }

    pinctrl_pm_select_sleep_state(&pPDev->dev);

    free_netdev(pNDev);

    INF("%s: atemsys driver removed: %s Instance %d\n", pPDev->name, pEthDrvDescPrivate->MacInfo.szIdent, pEthDrvDescPrivate->MacInfo.dwInstance);

    S_apEthDrvDescPrivate[pEthDrvDescPrivate->MacInfo.dwIndex] = NULL;

    return 0;
}

static int CleanUpEthernetDriverOnRelease(dev_node* pDevDesc)
{
    ATEMSYS_T_ETH_DRV_DESC_PRIVATE* pEthDrvDescPrivate = NULL;
    unsigned int i = 0;

    if (pDevDesc == NULL)
    {
        return 0;
    }

    for (i = 0; i < ATEMSYS_MAX_NUMBER_OF_ETHERNET_PORTS; i++)
    {
    
        pEthDrvDescPrivate = S_apEthDrvDescPrivate[i];
        if (NULL == pEthDrvDescPrivate)
        {
            continue;
        }

        if (pEthDrvDescPrivate->pDevDesc == pDevDesc)
        {
            INF("%s: Cleanup: pDevDesc = 0x%p\n", pEthDrvDescPrivate->pPDev->name, pDevDesc);

            /* ensure mdio bus and PHY are down */
            if ((NULL != pEthDrvDescPrivate->pPhyDev) || (NULL != pEthDrvDescPrivate->pMdioBus))
            {
                StopPhy(pEthDrvDescPrivate->pPDev);
            }
            /* clean descriptor */
            pEthDrvDescPrivate->pDevDesc = NULL;
        }
    }

    return 0;
}

static struct platform_device_id mac_devtype[] = {
    {
        .name = ATEMSYS_DT_DRIVER_NAME,
        .driver_data = 0,
    }, {
        /* sentinel */
    }
};


MODULE_DEVICE_TABLE(platform, mac_devtype);

static struct platform_driver mac_driver = {
    .driver    = {
        .name           = ATEMSYS_DT_DRIVER_NAME,
        .of_match_table = atemsys_ids,
    },
    .id_table  = mac_devtype,
    .probe     = EthernetDriverProbe,
    .remove    = EthernetDriverRemove,
};
#endif /* INCLUDE_ATEMSYS_DT_DRIVER */

/*
 * Initialize the module - Register the character device
 */
int init_module(void)
{
#if (defined CONFIG_XENO_COBALT)

   int major = rtdm_dev_register(&device);
   if (major < 0)
   {
      INF("Failed to register %s (err: %d)\n", device.label, major);
      return major;
   }
#else

   /* Register the character device */
   int major = register_chrdev(MAJOR_NUM, ATEMSYS_DEVICE_NAME, &Fops);
   if (major < 0)
   {
      INF("Failed to register %s (err: %d)\n",
             ATEMSYS_DEVICE_NAME, major);
      return major;
   }

#if (defined INCLUDE_ATEMSYS_DT_DRIVER)
    memset(S_apEthDrvDescPrivate ,0, ATEMSYS_MAX_NUMBER_OF_ETHERNET_PORTS * sizeof(ATEMSYS_T_ETH_DRV_DESC_PRIVATE*));
    platform_driver_register(&mac_driver);
#endif

   S_devClass = class_create(THIS_MODULE, ATEMSYS_DEVICE_NAME);
   if (IS_ERR(S_devClass))
   {
      INF("class_create failed\n");
      return -1;
   }

#if (LINUX_VERSION_CODE < KERNEL_VERSION(2,6,24))
   S_dev = class_device_create(S_devClass, NULL, MKDEV(MAJOR_NUM, 0), NULL, ATEMSYS_DEVICE_NAME);
#else
   S_dev = device_create(S_devClass, NULL, MKDEV(MAJOR_NUM, 0), NULL, ATEMSYS_DEVICE_NAME);
#endif

   if (IS_ERR(S_dev))
   {
      INF("device_create failed\n");
      return -1;
   }

   S_dev->coherent_dma_mask = DMA_BIT_MASK(32);
   if (!S_dev->dma_mask)
   {
      S_dev->dma_mask = &S_dev->coherent_dma_mask;
   }

#if (defined CONFIG_OF)
 #if (LINUX_VERSION_CODE >= KERNEL_VERSION(4,18,0))
   of_dma_configure(S_dev,S_dev->of_node, true);
 #else
   of_dma_configure(S_dev,S_dev->of_node);
 #endif
#endif

   INIT_LIST_HEAD(&S_devNode.list);

#endif /* CONFIG_XENO_COBALT */
   INF("%s v%s loaded\n", ATEMSYS_DEVICE_NAME, ATEMSYS_VERSION_STR);
   return 0;
}

/*
 * Cleanup - unregister the appropriate file from /proc
 */
void cleanup_module(void)
{
   INF("%s v%s unloaded\n", ATEMSYS_DEVICE_NAME, ATEMSYS_VERSION_STR);

#if (defined INCLUDE_ATEMSYS_DT_DRIVER)
    platform_driver_unregister(&mac_driver);
#endif

#if (defined CONFIG_OF)
   device_release_driver(S_dev); //see device_del() -> bus_remove_device()
#endif

#if (defined CONFIG_XENO_COBALT)
   rtdm_dev_unregister(&device);
#else
   device_destroy(S_devClass, MKDEV(MAJOR_NUM, 0));
   class_destroy(S_devClass);
   unregister_chrdev(MAJOR_NUM, ATEMSYS_DEVICE_NAME);
#endif /* CONFIG_XENO_COBALT */
}
