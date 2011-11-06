/*
 * (C) Copyright IBM Corporation 2006
 * Copyright (c) 2007, 2009, Oracle and/or its affiliates.
 * All Rights Reserved.
 *
 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of this software and associated documentation files (the "Software"),
 * to deal in the Software without restriction, including without limitation
 * on the rights to use, copy, modify, merge, publish, distribute, sub
 * license, and/or sell copies of the Software, and to permit persons to whom
 * the Software is furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice (including the next
 * paragraph) shall be included in all copies or substantial portions of the
 * Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NON-INFRINGEMENT.  IN NO EVENT SHALL
 * IBM AND/OR THEIR SUPPLIERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
 * FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER
 * DEALINGS IN THE SOFTWARE.
 */
/*
 * Solaris devfs interfaces
 */

#include <stdlib.h>
#include <strings.h>
#include <stdio.h>
#include <unistd.h>
#include <sys/types.h>
#include <fcntl.h>
#include <sys/mman.h>
#include <errno.h>
#include <sys/pci.h>
#include <libdevinfo.h>
#include "pci_tools.h"

#include "pciaccess.h"
#include "pciaccess_private.h"

/* #define DEBUG */

#define	MAX_DEVICES	256
#define	CELL_NUMS_1275	(sizeof(pci_regspec_t) / sizeof(uint_t))

typedef union {
    uint8_t bytes[16 * sizeof (uint32_t)];
    uint32_t dwords[16];
} pci_conf_hdr_t;

typedef struct i_devnode {
    uint8_t bus;
    uint8_t dev;
    uint8_t func;
    di_node_t node;
} i_devnode_t;

typedef struct nexus {
    int fd;
    int first_bus;
    int last_bus;
    char *path;			/* for errors/debugging; fd is all we need */
    struct nexus *next;
} nexus_t;

static nexus_t *nexus_list = NULL;
static int xsvc_fd = -1;

/*
 * Read config space in native processor endianness.  Endian-neutral
 * processing can then take place.  On big endian machines, MSB and LSB
 * of little endian data end up switched if read as little endian.
 * They are in correct order if read as big endian.
 */
#if defined(__sparc)
# define NATIVE_ENDIAN	PCITOOL_ACC_ATTR_ENDN_BIG
#elif defined(__x86)
# define NATIVE_ENDIAN	PCITOOL_ACC_ATTR_ENDN_LTL
#else
# error "ISA is neither __sparc nor __x86"
#endif

/*
 * Identify problematic southbridges.  These have device id 0x5249 and
 * vendor id 0x10b9.  Check for revision ID 0 and class code 060400 as well.
 * Values are little endian, so they are reversed for SPARC.
 *
 * Check for these southbridges on all architectures, as the issue is a
 * southbridge issue, independent of processor.
 *
 * If one of these is found during probing, skip probing other devs/funcs on
 * the rest of the bus, since the southbridge and all devs underneath will
 * otherwise disappear.
 */
#if (NATIVE_ENDIAN == PCITOOL_ACC_ATTR_ENDN_BIG)
# define U45_SB_DEVID_VID	0xb9104952
# define U45_SB_CLASS_RID	0x00000406
#else
# define U45_SB_DEVID_VID	0x524910b9
# define U45_SB_CLASS_RID	0x06040000
#endif

static int pci_device_solx_devfs_map_range(struct pci_device *dev,
    struct pci_device_mapping *map);

static int pci_device_solx_devfs_read_rom( struct pci_device * dev,
    void * buffer );

static int pci_device_solx_devfs_probe( struct pci_device * dev );

static int pci_device_solx_devfs_read( struct pci_device * dev, void * data,
    pciaddr_t offset, pciaddr_t size, pciaddr_t * bytes_read );

static int pci_device_solx_devfs_write( struct pci_device * dev,
    const void * data, pciaddr_t offset, pciaddr_t size,
    pciaddr_t * bytes_written );

static int probe_dev(nexus_t *nexus, pcitool_reg_t *prg_p,
		     struct pci_system *pci_sys);

static int do_probe(nexus_t *nexus, struct pci_system *pci_sys);

static int probe_nexus_node(di_node_t di_node, di_minor_t minor, void *arg);

static void pci_system_solx_devfs_destroy( void );

static int get_config_header(int fd, uint8_t bus_no, uint8_t dev_no,
			     uint8_t func_no, pci_conf_hdr_t *config_hdr_p);

int pci_system_solx_devfs_create( void );

static const struct pci_system_methods solx_devfs_methods = {
    .destroy = pci_system_solx_devfs_destroy,
    .destroy_device = NULL,
    .read_rom = pci_device_solx_devfs_read_rom,
    .probe = pci_device_solx_devfs_probe,
    .map_range = pci_device_solx_devfs_map_range,
    .unmap_range = pci_device_generic_unmap_range,

    .read = pci_device_solx_devfs_read,
    .write = pci_device_solx_devfs_write,

    .fill_capabilities = pci_fill_capabilities_generic
};

static nexus_t *
find_nexus_for_bus( int bus )
{
    nexus_t *nexus;

    for (nexus = nexus_list ; nexus != NULL ; nexus = nexus->next) {
	if ((bus >= nexus->first_bus) && (bus <= nexus->last_bus)) {
	    return nexus;
	}
    }
    return NULL;
}

#define GET_CONFIG_VAL_8(offset) (config_hdr.bytes[offset])
#define GET_CONFIG_VAL_16(offset) \
    (uint16_t) (GET_CONFIG_VAL_8(offset) + (GET_CONFIG_VAL_8(offset+1) << 8))
#define GET_CONFIG_VAL_32(offset) \
    (uint32_t) (GET_CONFIG_VAL_8(offset) + 		\
		(GET_CONFIG_VAL_8(offset+1) << 8) +	\
		(GET_CONFIG_VAL_8(offset+2) << 16) +	\
		(GET_CONFIG_VAL_8(offset+3) << 24))

/*
 * Release all the resources
 * Solaris version
 */
static void
pci_system_solx_devfs_destroy( void )
{
    /*
     * The memory allocated for pci_sys & devices in create routines
     * will be freed in pci_system_cleanup.
     * Need to free system-specific allocations here.
     */
    nexus_t *nexus, *next;

    for (nexus = nexus_list ; nexus != NULL ; nexus = next) {
	next = nexus->next;
	close(nexus->fd);
	free(nexus->path);
	free(nexus);
    }
    nexus_list = NULL;

    if (xsvc_fd >= 0) {
	close(xsvc_fd);
	xsvc_fd = -1;
    }
}

/*
 * Attempt to access PCI subsystem using Solaris's devfs interface.
 * Solaris version
 */
_pci_hidden int
pci_system_solx_devfs_create( void )
{
    int err = 0;
    di_node_t di_node;


    if (nexus_list != NULL) {
	return 0;
    }

    /*
     * Only allow MAX_DEVICES exists
     * I will fix it later to get
     * the total devices first
     */
    if ((pci_sys = calloc(1, sizeof (struct pci_system))) != NULL) {
	pci_sys->methods = &solx_devfs_methods;

	if ((pci_sys->devices =
	     calloc(MAX_DEVICES, sizeof (struct pci_device_private)))
	    != NULL) {

	    if ((di_node = di_init("/", DINFOCPYALL)) == DI_NODE_NIL) {
		err = errno;
		(void) fprintf(stderr, "di_init() failed: %s\n",
			       strerror(errno));
	    } else {
		(void) di_walk_minor(di_node, DDI_NT_REGACC, 0, pci_sys,
				     probe_nexus_node);
		di_fini(di_node);
	    }
	}
	else {
	    err = errno;
	}
    } else {
	err = errno;
    }

    if (err != 0) {
	if (pci_sys != NULL) {
	    free(pci_sys->devices);
	    free(pci_sys);
	    pci_sys = NULL;
	}
    }

    return (err);
}

/*
 * Retrieve first 16 dwords of device's config header, except for the first
 * dword.  First 16 dwords are defined by the PCI specification.
 */
static int
get_config_header(int fd, uint8_t bus_no, uint8_t dev_no, uint8_t func_no,
		  pci_conf_hdr_t *config_hdr_p)
{
    pcitool_reg_t cfg_prg;
    int i;
    int rval = 0;

    /* Prepare a local pcitool_reg_t so as to not disturb the caller's. */
    cfg_prg.offset = 0;
    cfg_prg.acc_attr = PCITOOL_ACC_ATTR_SIZE_4 + NATIVE_ENDIAN;
    cfg_prg.bus_no = bus_no;
    cfg_prg.dev_no = dev_no;
    cfg_prg.func_no = func_no;
    cfg_prg.barnum = 0;
    cfg_prg.user_version = PCITOOL_USER_VERSION;

    /* Get dwords 1-15 of config space. They must be read as uint32_t. */
    for (i = 1; i < (sizeof (pci_conf_hdr_t) / sizeof (uint32_t)); i++) {
	cfg_prg.offset += sizeof (uint32_t);
	if ((rval = ioctl(fd, PCITOOL_DEVICE_GET_REG, &cfg_prg)) != 0) {
	    break;
	}
	config_hdr_p->dwords[i] = (uint32_t)cfg_prg.data;
    }

    return (rval);
}


/*
 * Probe device's functions.  Modifies many fields in the prg_p.
 */
static int
probe_dev(nexus_t *nexus, pcitool_reg_t *prg_p, struct pci_system *pci_sys)
{
    pci_conf_hdr_t	config_hdr;
    boolean_t		multi_function_device;
    int8_t		func;
    int8_t		first_func = 0;
    int8_t		last_func = PCI_REG_FUNC_M >> PCI_REG_FUNC_SHIFT;
    int			rval = 0;
    struct pci_device *	pci_base;

    /*
     * Loop through at least func=first_func.  Continue looping through
     * functions if there are no errors and the device is a multi-function
     * device.
     *
     * (Note, if first_func == 0, header will show whether multifunction
     * device and set multi_function_device.  If first_func != 0, then we
     * will force the loop as the user wants a specific function to be
     * checked.
     */
    for (func = first_func, multi_function_device = B_FALSE;
	 ((func <= last_func) &&
	  ((func == first_func) || (multi_function_device)));
	 func++) {
	prg_p->func_no = func;

	/*
	 * Four things can happen here:
	 *
	 * 1) ioctl comes back as EFAULT and prg_p->status is
	 *    PCITOOL_INVALID_ADDRESS.  There is no device at this location.
	 *
	 * 2) ioctl comes back successful and the data comes back as
	 *    zero.  Config space is mapped but no device responded.
	 *
	 * 3) ioctl comes back successful and the data comes back as
	 *    non-zero.  We've found a device.
	 *
	 * 4) Some other error occurs in an ioctl.
	 */

	prg_p->status = PCITOOL_SUCCESS;
	prg_p->offset = 0;
	prg_p->data = 0;
	prg_p->user_version = PCITOOL_USER_VERSION;

	errno = 0;
	if (((rval = ioctl(nexus->fd, PCITOOL_DEVICE_GET_REG, prg_p)) != 0) ||
	    (prg_p->data == 0xffffffff)) {

	    /*
	     * Accept errno == EINVAL along with status of
	     * PCITOOL_OUT_OF_RANGE because some systems
	     * don't implement the full range of config space.
	     * Leave the loop quietly in this case.
	     */
	    if ((errno == EINVAL) ||
		(prg_p->status == PCITOOL_OUT_OF_RANGE)) {
		break;
	    }

	    /*
	     * Exit silently with ENXIO as this means that there are
	     * no devices under the pci root nexus.
	     */
	    else if ((errno == ENXIO) &&
		     (prg_p->status == PCITOOL_IO_ERROR)) {
		break;
	    }

	    /*
	     * Expect errno == EFAULT along with status of
	     * PCITOOL_INVALID_ADDRESS because there won't be
	     * devices at each stop.  Quit on any other error.
	     */
	    else if (((errno != EFAULT) ||
		      (prg_p->status != PCITOOL_INVALID_ADDRESS)) &&
		     (prg_p->data != 0xffffffff)) {
		break;
	    }

	    /*
	     * If no function at this location,
	     * just advance to the next function.
	     */
	    else {
		rval = 0;
	    }

	    /*
	     * Data came back as 0.
	     * Treat as unresponsive device and check next device.
	     */
	} else if (prg_p->data == 0) {
	    rval = 0;
	    break;	/* Func loop. */

	    /* Found something. */
	} else {
	    config_hdr.dwords[0] = (uint32_t)prg_p->data;

	    /* Get the rest of the PCI header. */
	    if ((rval = get_config_header(nexus->fd, prg_p->bus_no,
					  prg_p->dev_no, prg_p->func_no,
					  &config_hdr)) != 0) {
		break;
	    }

	    /*
	     * Special case for the type of Southbridge found on
	     * Ultra-45 and other sun4u fire workstations.
	     */
	    if ((config_hdr.dwords[0] == U45_SB_DEVID_VID) &&
		(config_hdr.dwords[2] == U45_SB_CLASS_RID)) {
		rval = ECANCELED;
		break;
	    }

	    /*
	     * Found one device with bus number, device number and
	     * function number.
	     */

	    pci_base = &pci_sys->devices[pci_sys->num_devices].base;

	    /*
	     * Domain is peer bus??
	     */
	    pci_base->domain = 0;
	    pci_base->bus = prg_p->bus_no;
	    pci_base->dev = prg_p->dev_no;
	    pci_base->func = func;

	    /*
	     * for the format of device_class, see struct pci_device;
	     */

	    pci_base->device_class =
		(GET_CONFIG_VAL_8(PCI_CONF_BASCLASS) << 16) |
		(GET_CONFIG_VAL_8(PCI_CONF_SUBCLASS) << 8) |
		GET_CONFIG_VAL_8(PCI_CONF_PROGCLASS);

	    pci_base->revision		= GET_CONFIG_VAL_8(PCI_CONF_REVID);
	    pci_base->vendor_id		= GET_CONFIG_VAL_16(PCI_CONF_VENID);
	    pci_base->device_id		= GET_CONFIG_VAL_16(PCI_CONF_DEVID);
	    pci_base->subvendor_id 	= GET_CONFIG_VAL_16(PCI_CONF_SUBVENID);
	    pci_base->subdevice_id 	= GET_CONFIG_VAL_16(PCI_CONF_SUBSYSID);

	    pci_sys->devices[pci_sys->num_devices].header_type
					= GET_CONFIG_VAL_8(PCI_CONF_HEADER);

#ifdef DEBUG
	    fprintf(stderr,
		    "nexus = %s, busno = %x, devno = %x, funcno = %x\n",
		    nexus->path, prg_p->bus_no, prg_p->dev_no, func);
#endif

	    if (pci_sys->num_devices < (MAX_DEVICES - 1)) {
		pci_sys->num_devices++;
	    } else {
		(void) fprintf(stderr,
			       "Maximum number of PCI devices found,"
			       " discarding additional devices\n");
	    }


	    /*
	     * Accommodate devices which state their
	     * multi-functionality only in their function 0 config
	     * space.  Note multi-functionality throughout probing
	     * of all of this device's functions.
	     */
	    if (config_hdr.bytes[PCI_CONF_HEADER] & PCI_HEADER_MULTI) {
		multi_function_device = B_TRUE;
	    }
	}
    }

    return (rval);
}

/*
 * This function is called from di_walk_minor() when any PROBE is processed
 */
static int
probe_nexus_node(di_node_t di_node, di_minor_t minor, void *arg)
{
    struct pci_system *pci_sys = (struct pci_system *) arg;
    char *nexus_name;
    nexus_t *nexus;
    int fd;
    char nexus_path[MAXPATHLEN];

    di_prop_t prop;
    char *strings;
    int *ints;
    int numval;
    int pci_node = 0;
    int first_bus = 0, last_bus = PCI_REG_BUS_G(PCI_REG_BUS_M);

#ifdef DEBUG
    nexus_name = di_devfs_minor_path(minor);
    fprintf(stderr, "-- device name: %s\n", nexus_name);
#endif

    for (prop = di_prop_next(di_node, NULL); prop != NULL;
	 prop = di_prop_next(di_node, prop)) {

	const char *prop_name = di_prop_name(prop);

#ifdef DEBUG
	fprintf(stderr, "   property: %s\n", prop_name);
#endif

	if (strcmp(prop_name, "device_type") == 0) {
	    numval = di_prop_strings(prop, &strings);
	    if (numval != 1 || strncmp(strings, "pci", 3) != 0) {
		/* not a PCI node, bail */
		return (DI_WALK_CONTINUE);
	    }
	    pci_node = 1;
	}
	else if (strcmp(prop_name, "class-code") == 0) {
	    /* not a root bus node, bail */
	    return (DI_WALK_CONTINUE);
	}
	else if (strcmp(prop_name, "bus-range") == 0) {
	    numval = di_prop_ints(prop, &ints);
	    if (numval == 2) {
		first_bus = ints[0];
		last_bus = ints[1];
	    }
	}
    }

#ifdef __x86  /* sparc pci nodes don't have the device_type set */
    if (pci_node != 1)
	return (DI_WALK_CONTINUE);
#endif

    /* we have a PCI root bus node. */
    nexus = calloc(1, sizeof(nexus_t));
    if (nexus == NULL) {
	(void) fprintf(stderr, "Error allocating memory for nexus: %s\n",
		       strerror(errno));
	return (DI_WALK_TERMINATE);
    }
    nexus->first_bus = first_bus;
    nexus->last_bus = last_bus;

    nexus_name = di_devfs_minor_path(minor);
    if (nexus_name == NULL) {
	(void) fprintf(stderr, "Error getting nexus path: %s\n",
		       strerror(errno));
	free(nexus);
	return (DI_WALK_CONTINUE);
    }

    snprintf(nexus_path, sizeof(nexus_path), "/devices%s", nexus_name);
    di_devfs_path_free(nexus_name);

#ifdef DEBUG
    fprintf(stderr, "nexus = %s, bus-range = %d - %d\n",
	    nexus_path, first_bus, last_bus);
#endif

    if ((fd = open(nexus_path, O_RDWR)) >= 0) {
	nexus->fd = fd;
	nexus->path = strdup(nexus_path);
	if ((do_probe(nexus, pci_sys) != 0) && (errno != ENXIO)) {
	    (void) fprintf(stderr, "Error probing node %s: %s\n",
			   nexus_path, strerror(errno));
	    (void) close(fd);
	    free(nexus->path);
	    free(nexus);
	} else {
	    nexus->next = nexus_list;
	    nexus_list = nexus;
	}
    } else {
	(void) fprintf(stderr, "Error opening %s: %s\n",
		       nexus_path, strerror(errno));
	free(nexus);
    }

    return DI_WALK_CONTINUE;
}


/*
 * Solaris version
 * Probe a given nexus config space for devices.
 *
 * fd is the file descriptor of the nexus.
 * input_args contains commandline options as specified by the user.
 */
static int
do_probe(nexus_t *nexus, struct pci_system *pci_sys)
{
    pcitool_reg_t prg;
    uint32_t bus;
    uint8_t dev;
    uint32_t last_bus = nexus->last_bus;
    uint8_t last_dev = PCI_REG_DEV_M >> PCI_REG_DEV_SHIFT;
    uint8_t first_bus = nexus->first_bus;
    uint8_t first_dev = 0;
    int rval = 0;

    prg.barnum = 0;	/* Config space. */

    /* Must read in 4-byte quantities. */
    prg.acc_attr = PCITOOL_ACC_ATTR_SIZE_4 + NATIVE_ENDIAN;

    prg.data = 0;

    /*
     * Loop through all valid bus / dev / func combinations to check for
     * all devices, with the following exceptions:
     *
     * When nothing is found at function 0 of a bus / dev combination, skip
     * the other functions of that bus / dev combination.
     *
     * When a found device's function 0 is probed and it is determined that
     * it is not a multifunction device, skip probing of that device's
     * other functions.
     */
    for (bus = first_bus; ((bus <= last_bus) && (rval == 0)); bus++) {
	prg.bus_no = (uint8_t)bus;

	for (dev = first_dev; ((dev <= last_dev) && (rval == 0)); dev++) {
	    prg.dev_no = dev;
	    rval = probe_dev(nexus, &prg, pci_sys);
	}

	/*
	 * Ultra-45 southbridge workaround:
	 * ECANCELED tells to skip to the next bus.
	 */
	if (rval == ECANCELED) {
	    rval = 0;
	}
    }

    return (rval);
}

static int
find_target_node(di_node_t node, void *arg)
{
    int *regbuf = NULL;
    int len = 0;
    uint32_t busno, funcno, devno;
    i_devnode_t *devnode = (i_devnode_t *)arg;

    /*
     * Test the property functions, only for testing
     */
    /*
    void *prop = DI_PROP_NIL;

    (void) fprintf(stderr, "start of node 0x%x\n", node->nodeid);
    while ((prop = di_prop_hw_next(node, prop)) != DI_PROP_NIL) {
	int i;
	(void) fprintf(stderr, "name=%s: ", di_prop_name(prop));
	len = 0;
	if (!strcmp(di_prop_name(prop), "reg")) {
	    len = di_prop_ints(prop, &regbuf);
	}
	for (i = 0; i < len; i++) {
	    fprintf(stderr, "0x%0x.", regbuf[i]);
	}
	fprintf(stderr, "\n");
    }
    (void) fprintf(stderr, "end of node 0x%x\n", node->nodeid);
    */

    len = di_prop_lookup_ints(DDI_DEV_T_ANY, node, "reg", &regbuf);

    if (len <= 0) {
#ifdef DEBUG
	fprintf(stderr, "error = %x\n", errno);
	fprintf(stderr, "can not find assigned-address\n");
#endif
	return (DI_WALK_CONTINUE);
    }

    busno = PCI_REG_BUS_G(regbuf[0]);
    devno = PCI_REG_DEV_G(regbuf[0]);
    funcno = PCI_REG_FUNC_G(regbuf[0]);

    if ((busno == devnode->bus) &&
	(devno == devnode->dev) &&
	(funcno == devnode->func)) {
	devnode->node = node;

	return (DI_WALK_TERMINATE);
    }

    return (DI_WALK_CONTINUE);
}

/*
 * Solaris version
 */
static int
pci_device_solx_devfs_probe( struct pci_device * dev )
{
    uint8_t  config[256];
    int err;
    di_node_t rnode = DI_NODE_NIL;
    i_devnode_t args = { 0, 0, 0, DI_NODE_NIL };
    int *regbuf;
    pci_regspec_t *reg;
    int i;
    pciaddr_t bytes;
    int len = 0;
    uint ent = 0;

    err = pci_device_solx_devfs_read( dev, config, 0, 256, & bytes );

    if ( bytes >= 64 ) {
	struct pci_device_private *priv =
	    (struct pci_device_private *) dev;

	dev->vendor_id = (uint16_t)config[0] + ((uint16_t)config[1] << 8);
	dev->device_id = (uint16_t)config[2] + ((uint16_t)config[3] << 8);
	dev->device_class = (uint32_t)config[9] +
	    ((uint32_t)config[10] << 8) +
	    ((uint16_t)config[11] << 16);

	/*
	 * device class code is already there.
	 * see probe_dev function.
	 */
	dev->revision = config[8];
	dev->subvendor_id = (uint16_t)config[44] + ((uint16_t)config[45] << 8);
	dev->subdevice_id = (uint16_t)config[46] + ((uint16_t)config[47] << 8);
	dev->irq = config[60];

	priv->header_type = config[14];
	/*
	 * starting to find if it is MEM/MEM64/IO
	 * using libdevinfo
	 */
	if ((rnode = di_init("/", DINFOCPYALL)) == DI_NODE_NIL) {
	    err = errno;
	    (void) fprintf(stderr, "di_init failed: %s\n", strerror(errno));
	} else {
	    args.bus = dev->bus;
	    args.dev = dev->dev;
	    args.func = dev->func;
	    (void) di_walk_node(rnode, DI_WALK_CLDFIRST,
				(void *)&args, find_target_node);
	}
    }
    if (args.node != DI_NODE_NIL) {
	/*
	 * It will succeed for sure, because it was
	 * successfully called in find_target_node
	 */
	len = di_prop_lookup_ints(DDI_DEV_T_ANY, args.node,
				  "assigned-addresses",
				  &regbuf);

    }

    if (len <= 0)
	goto cleanup;


    /*
     * how to find the size of rom???
     * if the device has expansion rom,
     * it must be listed in the last
     * cells because solaris find probe
     * the base address from offset 0x10
     * to 0x30h. So only check the last
     * item.
     */
    reg = (pci_regspec_t *)&regbuf[len - CELL_NUMS_1275];
    if (PCI_REG_REG_G(reg->pci_phys_hi) == PCI_CONF_ROM) {
	/*
	 * rom can only be 32 bits
	 */
	dev->rom_size = reg->pci_size_low;
	len = len - CELL_NUMS_1275;
    }
    else {
	/*
	 * size default to 64K and base address
	 * default to 0xC0000
	 */
	dev->rom_size = 0x10000;
    }

    /*
     * Solaris has its own BAR index.
     * Linux give two region slot for 64 bit address.
     */
    for (i = 0; i < len; i = i + CELL_NUMS_1275) {

	reg = (pci_regspec_t *)&regbuf[i];
	ent = reg->pci_phys_hi & 0xff;
	/*
	 * G35 broken in BAR0
	 */
	ent = (ent - PCI_CONF_BASE0) >> 2;
	if (ent >= 6) {
	    fprintf(stderr, "error ent = %d\n", ent);
	    break;
	}

	/*
	 * non relocatable resource is excluded
	 * such like 0xa0000, 0x3b0. If it is met,
	 * the loop is broken;
	 */
	if (!PCI_REG_REG_G(reg->pci_phys_hi))
	    break;

	if (reg->pci_phys_hi & PCI_PREFETCH_B) {
	    dev->regions[ent].is_prefetchable = 1;
	}


	/*
	 * We split the shift count 32 into two 16 to
	 * avoid the complaining of the compiler
	 */
	dev->regions[ent].base_addr = reg->pci_phys_low +
	    ((reg->pci_phys_mid << 16) << 16);
	dev->regions[ent].size = reg->pci_size_low +
	    ((reg->pci_size_hi << 16) << 16);

	switch (reg->pci_phys_hi & PCI_REG_ADDR_M) {
	    case PCI_ADDR_IO:
		dev->regions[ent].is_IO = 1;
		break;
	    case PCI_ADDR_MEM32:
		break;
	    case PCI_ADDR_MEM64:
		dev->regions[ent].is_64 = 1;
		/*
		 * Skip one slot for 64 bit address
		 */
		break;
	}
    }

  cleanup:
    if (rnode != DI_NODE_NIL) {
	di_fini(rnode);
    }
    return (err);
}

/*
 * Solaris version: read the VGA ROM data
 */
static int
pci_device_solx_devfs_read_rom( struct pci_device * dev, void * buffer )
{
    int err;
    struct pci_device_mapping prom = {
	.base = 0xC0000,
	.size = dev->rom_size,
	.flags = 0
    };

    err = pci_device_solx_devfs_map_range(dev, &prom);
    if (err == 0) {
	(void) bcopy(prom.memory, buffer, dev->rom_size);

	if (munmap(prom.memory, dev->rom_size) == -1) {
	    err = errno;
	}
    }
    return err;
}

/*
 * solaris version: Read the configurations space of the devices
 */
static int
pci_device_solx_devfs_read( struct pci_device * dev, void * data,
			     pciaddr_t offset, pciaddr_t size,
			     pciaddr_t * bytes_read )
{
    pcitool_reg_t cfg_prg;
    int err = 0;
    int i = 0;
    nexus_t *nexus = find_nexus_for_bus(dev->bus);

    *bytes_read = 0;

    if ( nexus == NULL ) {
	return ENODEV;
    }

    cfg_prg.offset = offset;
    cfg_prg.acc_attr = PCITOOL_ACC_ATTR_SIZE_1 + NATIVE_ENDIAN;
    cfg_prg.bus_no = dev->bus;
    cfg_prg.dev_no = dev->dev;
    cfg_prg.func_no = dev->func;
    cfg_prg.barnum = 0;
    cfg_prg.user_version = PCITOOL_USER_VERSION;

    for (i = 0; i < size; i += PCITOOL_ACC_ATTR_SIZE(PCITOOL_ACC_ATTR_SIZE_1))
    {
	cfg_prg.offset = offset + i;

	if ((err = ioctl(nexus->fd, PCITOOL_DEVICE_GET_REG, &cfg_prg)) != 0) {
	    fprintf(stderr, "read bdf<%s,%x,%x,%x,%llx> config space failure\n",
		    nexus->path,
		    cfg_prg.bus_no,
		    cfg_prg.dev_no,
		    cfg_prg.func_no,
		    cfg_prg.offset);
	    fprintf(stderr, "Failure cause = %x\n", err);
	    break;
	}

	((uint8_t *)data)[i] = (uint8_t)cfg_prg.data;
	/*
	 * DWORDS Offset or bytes Offset ??
	 */
    }
    *bytes_read = i;

    return (err);
}

/*
 * Solaris version
 */
static int
pci_device_solx_devfs_write( struct pci_device * dev, const void * data,
			     pciaddr_t offset, pciaddr_t size,
			     pciaddr_t * bytes_written )
{
    pcitool_reg_t cfg_prg;
    int err = 0;
    int cmd;
    nexus_t *nexus = find_nexus_for_bus(dev->bus);

    if ( bytes_written != NULL ) {
	*bytes_written = 0;
    }

    if ( nexus == NULL ) {
	return ENODEV;
    }

    cfg_prg.offset = offset;
    switch (size) {
        case 1:
	    cfg_prg.acc_attr = PCITOOL_ACC_ATTR_SIZE_1 + NATIVE_ENDIAN;
	    break;
        case 2:
	    cfg_prg.acc_attr = PCITOOL_ACC_ATTR_SIZE_2 + NATIVE_ENDIAN;
	    break;
        case 4:
	    cfg_prg.acc_attr = PCITOOL_ACC_ATTR_SIZE_4 + NATIVE_ENDIAN;
	    break;
        case 8:
	    cfg_prg.acc_attr = PCITOOL_ACC_ATTR_SIZE_8 + NATIVE_ENDIAN;
	    break;
        default:
	    return EINVAL;
    }
    cfg_prg.bus_no = dev->bus;
    cfg_prg.dev_no = dev->dev;
    cfg_prg.func_no = dev->func;
    cfg_prg.barnum = 0;
    cfg_prg.user_version = PCITOOL_USER_VERSION;
    cfg_prg.data = *((uint64_t *)data);

    /*
     * Check if this device is bridge device.
     * If it is, it is also a nexus node???
     * It seems that there is no explicit
     * PCI nexus device for X86, so not applicable
     * from pcitool_bus_reg_ops in pci_tools.c
     */
    cmd = PCITOOL_DEVICE_SET_REG;

    if ((err = ioctl(nexus->fd, cmd, &cfg_prg)) != 0) {
	return (err);
    }
    *bytes_written = size;

    return (err);
}


/**
 * Map a memory region for a device using /dev/xsvc.
 *
 * \param dev   Device whose memory region is to be mapped.
 * \param map   Parameters of the mapping that is to be created.
 *
 * \return
 * Zero on success or an \c errno value on failure.
 */
static int
pci_device_solx_devfs_map_range(struct pci_device *dev,
				struct pci_device_mapping *map)
{
    const int prot = ((map->flags & PCI_DEV_MAP_FLAG_WRITABLE) != 0)
			? (PROT_READ | PROT_WRITE) : PROT_READ;
    int err = 0;

    /*
     * Still used xsvc to do the user space mapping
     */
    if (xsvc_fd < 0) {
	if ((xsvc_fd = open("/dev/xsvc", O_RDWR)) < 0) {
	    err = errno;
	    (void) fprintf(stderr, "can not open /dev/xsvc: %s\n",
			   strerror(errno));
	    return err;
	}
    }

    map->memory = mmap(NULL, map->size, prot, MAP_SHARED, xsvc_fd, map->base);
    if (map->memory == MAP_FAILED) {
	err = errno;

	(void) fprintf(stderr, "map rom region =%llx failed: %s\n",
		       map->base, strerror(errno));
    }

    return err;
}
