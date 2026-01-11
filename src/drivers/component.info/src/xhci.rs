// Minimal but non-trivial xHCI skeleton with:
// - PCI init
// - command ring
// - event ring (single segment)
// - per-endpoint transfer rings
// - basic port enumeration
// - interrupt_in()
// - control_transfer() / control_in() / control_out()

use alloc::{boxed::Box, format, vec, vec::Vec};
use core::ptr::NonNull;
use spin::Mutex;

use crate::pci::PciDevice;
use crate::usb::device::{UsbDevice, PortSpeed};
use crate::vga_buffer;

// ============ TRB & constants ============

#[repr(C, packed)]
#[derive(Clone, Copy, Default)]
pub struct Trb {
    pub dword0: u32,
    pub dword1: u32,
    pub dword2: u32,
    pub dword3: u32,
}

// TRB types
const TRB_TYPE_NORMAL: u32 = 1;
const TRB_TYPE_SETUP_STAGE: u32 = 2;
const TRB_TYPE_DATA_STAGE: u32 = 3;
const TRB_TYPE_STATUS_STAGE: u32 = 4;
const TRB_TYPE_LINK: u32 = 6;
const TRB_TYPE_ENABLE_SLOT: u32 = 9;
const TRB_TYPE_ADDRESS_DEVICE: u32 = 11;
const TRB_TYPE_TRANSFER_EVENT: u32 = 32;
const TRB_TYPE_COMMAND_COMPLETION_EVENT: u32 = 33;
const TRB_TYPE_NOOP: u32 = 23;

// helper: TRB type field (bits 10:15 in DW3)
#[inline]
fn trb_set_type(t: u32) -> u32 {
    t << 10
}

// Cycle bit
const TRB_CYCLE: u32 = 1;

// Control-transfer related flags
const TRB_IDT: u32 = 1 << 6;  // Immediate Data
const TRB_IOC: u32 = 1 << 5;  // Interrupt On Completion

// For Setup TRB: Transfer Type (TRT) = bits 16-17
const TRT_NO_DATA: u32 = 0;
const TRT_OUT_DATA: u32 = 2;
const TRT_IN_DATA: u32 = 3;

// ============ Registers ============

#[repr(C, packed)]
pub struct XhciCapRegs {
    pub caplength: u8,
    pub reserved: u8,
    pub hci_version: u16,
    pub hcsparams1: u32,
    pub hcsparams2: u32,
    pub hcsparams3: u32,
    pub hccparams1: u32,
    pub dboff: u32,
    pub rtsoff: u32,
    pub hccparams2: u32,
}

#[repr(C, packed)]
pub struct XhciOpRegs {
    pub usbcmd: u32,
    pub usbsts: u32,
    pub pagesize: u32,
    pub reserved0: [u32; 2],
    pub dnctrl: u32,
    pub crcr: u64,
    pub reserved1: [u32; 4],
    pub dcbaap: u64,
    pub config: u32,
}

#[repr(C, packed)]
pub struct XhciRuntimeRegs {
    pub mfindex: u32,
    pub reserved: [u32; 7],
}

#[repr(C, packed)]
pub struct InterrupterRegs {
    pub iman: u32,
    pub imod: u32,
    pub erstsz: u32,
    pub rsvd: u32,
    pub erstba: u64,
    pub erdp: u64,
}

pub struct XhciRegs {
    pub cap: NonNull<XhciCapRegs>,
    pub op: NonNull<XhciOpRegs>,
    pub rt: NonNull<XhciRuntimeRegs>,
    pub db: NonNull<u32>, // doorbell array base
}

const PORTSC_BASE_OFFSET: usize = 0x400;
const PORT_REG_STRIDE: usize = 0x10;
const INTERRUPTER0_OFFSET: usize = 0x20;

impl XhciRegs {
    /// # Safety
    /// `base` must be valid mapped MMIO for xHCI.
    unsafe fn new(base: *mut u8) -> Self {
        let cap = base as *mut XhciCapRegs;
        let cap_ref = &*cap;

        let caplength = cap_ref.caplength as usize;
        let op = base.add(caplength) as *mut XhciOpRegs;

        let dboff = (cap_ref.dboff & !0b11) as usize;
        let rtsoff = (cap_ref.rtsoff & !0b1111) as usize;

        let rt = base.add(rtsoff) as *mut XhciRuntimeRegs;
        let db = base.add(dboff) as *mut u32;

        XhciRegs {
            cap: NonNull::new_unchecked(cap),
            op: NonNull::new_unchecked(op),
            rt: NonNull::new_unchecked(rt),
            db: NonNull::new_unchecked(db),
        }
    }

    fn cap(&self) -> &XhciCapRegs {
        unsafe { self.cap.as_ref() }
    }

    fn op(&self) -> &XhciOpRegs {
        unsafe { self.op.as_ref() }
    }

    fn op_mut(&mut self) -> &mut XhciOpRegs {
        unsafe { self.op.as_mut() }
    }

    fn interrupter0(&self) -> &mut InterrupterRegs {
        let rt_base = self.rt.as_ptr() as *mut u8;
        let intr0 = unsafe { rt_base.add(INTERRUPTER0_OFFSET) as *mut InterrupterRegs };
        unsafe { &mut *intr0 }
    }

    fn portsc_ptr(&self, port_index: u8) -> *mut u32 {
        let op_base = self.op.as_ptr() as *mut u8;
        let offset = PORTSC_BASE_OFFSET + (port_index as usize * PORT_REG_STRIDE);
        unsafe { op_base.add(offset) as *mut u32 }
    }

    fn doorbell_ptr(&self, index: u8) -> *mut u32 {
        unsafe { self.db.as_ptr().add(index as usize) }
    }
}

// ============ Rings ============

pub struct XhciRing {
    pub trbs: &'static mut [Trb],
    pub enqueue_idx: usize,
    pub dequeue_idx: usize,
    pub cycle_state: bool,
}

impl XhciRing {
    fn new(count: usize) -> Result<Self, &'static str> {
        if count == 0 {
            return Err("[xHCI] XhciRing::new: count = 0");
        }
        let mut vec_trb: Vec<Trb> = vec![Trb::default(); count + 1];
        // Last TRB is Link TRB
        let last = vec_trb.len() - 1;
        vec_trb[last].dword3 = trb_set_type(TRB_TYPE_LINK) | TRB_CYCLE;

        let boxed: Box<[Trb]> = vec_trb.into_boxed_slice();
        let slice: &'static mut [Trb] = Box::leak(boxed);

        Ok(Self {
            trbs: slice,
            enqueue_idx: 0,
            dequeue_idx: 0,
            cycle_state: true,
        })
    }

    fn push_trb(&mut self, mut trb: Trb) {
        trb.dword3 |= (self.cycle_state as u32) & TRB_CYCLE;
        self.trbs[self.enqueue_idx] = trb;

        self.enqueue_idx += 1;
        if self.enqueue_idx >= self.trbs.len() - 1 {
            // hit link TRB
            self.trbs[self.enqueue_idx].dword3 ^= TRB_CYCLE;
            self.enqueue_idx = 0;
            self.cycle_state = !self.cycle_state;
        }
    }

    fn pop_trb(&mut self) -> Option<Trb> {
        let trb = self.trbs[self.dequeue_idx];
        let trb_cycle = trb.dword3 & TRB_CYCLE;
        if trb_cycle != (self.cycle_state as u32) {
            return None;
        }
        self.dequeue_idx += 1;
        if self.dequeue_idx >= self.trbs.len() - 1 {
            self.dequeue_idx = 0;
            self.cycle_state = !self.cycle_state;
        }
        Some(trb)
    }
}

#[repr(C, packed)]
pub struct ErstEntry {
    pub seg_base_lo: u32,
    pub seg_base_hi: u32,
    pub seg_size: u32,
    pub rsvd: u32,
}

pub struct EventRing {
    pub ring: XhciRing,
    pub erst: &'static mut [ErstEntry; 1],
}

// ============ Controller struct ============

pub struct XhciController {
    pub pci_dev: PciDevice,
    pub regs: XhciRegs,
    pub max_slots: u8,
    pub max_ports: u8,

    pub cmd_ring: XhciRing,
    pub evt_ring: EventRing,
    pub dcbaa: &'static mut [u64],

    /// Per-slot / per-endpoint transfer rings.
    /// Very simple model: [slot_id][endpoint_id] = optional ring.
    pub ep_rings: Vec<Vec<Option<XhciRing>>>,
}

static XHCI: Mutex<Option<&'static mut XhciController>> = Mutex::new(None);

// ============ Public entry points ============

pub fn init_from_pci(dev: &PciDevice, bar0: u32) -> Result<(), &'static str> {
    if bar0 & 0x1 != 0 {
        return Err("[xHCI] BAR0 is I/O space; expected MMIO");
    }
    let phys_base = (bar0 & 0xFFFF_FFF0) as u64;

    unsafe {
        let mmio_base = phys_base as *mut u8; // TODO: map phys → virt properly
        if mmio_base.is_null() {
            return Err("[xHCI] MMIO base is null");
        }

        let mut regs = XhciRegs::new(mmio_base);
        let cap = regs.cap();

        let hcs1 = cap.hcsparams1;
        let max_slots = (hcs1 & 0xFF) as u8;
        let max_ports = ((hcs1 >> 24) & 0xFF) as u8;

        let msg = format!(
            "[xHCI] HCI version={:04x}, max_slots={}, max_ports={}, dboff=0x{:08x}, rtsoff=0x{:08x}",
            cap.hci_version,
            max_slots,
            max_ports,
            cap.dboff,
            cap.rtsoff,
        );
        vga_buffer::log_line(&msg);

        let cmd_ring = XhciRing::new(256)?;
        let evt_ring = init_event_ring(&mut regs)?;
        let dcbaa = alloc_dcbaa(max_slots)?;

        init_controller_hw(&mut regs, &cmd_ring, &evt_ring, &dcbaa, max_slots)?;

        // Allocate ep_rings: (max_slots+1) x 32 endpoints
        let mut ep_rings = Vec::new();
        for _ in 0..=max_slots {
            let mut slot_eps = Vec::new();
            for _ in 0..32 {
                slot_eps.push(None);
            }
            ep_rings.push(slot_eps);
        }

        let ctrl = Box::new(XhciController {
            pci_dev: *dev,
            regs,
            max_slots,
            max_ports,
            cmd_ring,
            evt_ring,
            dcbaa,
            ep_rings,
        });

        let ctrl_ref: &'static mut XhciController = Box::leak(ctrl);
        *XHCI.lock() = Some(ctrl_ref);
    }

    Ok(())
}

fn alloc_dcbaa(max_slots: u8) -> Result<&'static mut [u64], &'static str> {
    let entries = max_slots as usize + 1;
    if entries == 0 {
        return Err("[xHCI] alloc_dcbaa: 0 entries");
    }
    let boxed: Box<[u64]> = vec![0u64; entries].into_boxed_slice();
    Ok(Box::leak(boxed))
}

unsafe fn init_event_ring(
    regs: &mut XhciRegs,
) -> Result<EventRing, &'static str> {
    // Event ring: 256 TRBs
    let ring = XhciRing::new(256)?;

    // ERST: 1 entry
    let mut erst_box = Box::new([ErstEntry {
        seg_base_lo: 0,
        seg_base_hi: 0,
        seg_size: ring.trbs.len() as u32,
        rsvd: 0,
    }]);
    let ring_ptr = ring.trbs.as_ptr() as u64;

    erst_box[0].seg_base_lo = ring_ptr as u32;
    erst_box[0].seg_base_hi = (ring_ptr >> 32) as u32;

    let erst: &'static mut [ErstEntry; 1] = Box::leak(erst_box);

    let intr0 = regs.interrupter0();
    intr0.erstsz = 1;
    intr0.erstba = erst.as_ptr() as u64;
    intr0.erdp = ring_ptr;

    // Unmask interrupter 0
    intr0.iman |= 1;

    Ok(EventRing { ring, erst })
}

unsafe fn init_controller_hw(
    regs: &mut XhciRegs,
    cmd_ring: &XhciRing,
    evt_ring: &EventRing,
    dcbaa: &[u64],
    max_slots: u8,
) -> Result<(), &'static str> {
    let op = regs.op_mut();

    const USBCMD_RUN_STOP: u32 = 1 << 0;
    const USBCMD_HCRST: u32 = 1 << 1;
    const USBSTS_HCH: u32 = 1 << 0;

    // Stop if running
    if op.usbcmd & USBCMD_RUN_STOP != 0 {
        op.usbcmd &= !USBCMD_RUN_STOP;
        // Wait until halted
        let mut tries = 0;
        while op.usbsts & USBSTS_HCH == 0 {
            tries += 1;
            if tries > 1_000_000 {
                break;
            }
        }
    }

    // Reset
    op.usbcmd |= USBCMD_HCRST;
    let mut tries = 0;
    while op.usbcmd & USBCMD_HCRST != 0 {
        tries += 1;
        if tries > 1_000_000 {
            vga_buffer::log_line("[xHCI] HCRST did not clear (timeout)");
            break;
        }
    }

    // Program DCBAAP (virt used as phys for now)
    let dcbaa_ptr = dcbaa.as_ptr() as u64;
    op.dcbaap = dcbaa_ptr;

    // Program CRCR (virt used as phys)
    let cr_base = cmd_ring.trbs.as_ptr() as u64;
    op.crcr = cr_base | (cmd_ring.cycle_state as u64);

    // Set max slots
    op.config = (max_slots as u32) & 0xFF;

    vga_buffer::log_line("[xHCI] Controller basic registers configured.");

    // Clear status bits
    op.usbsts |= op.usbsts;

    // Start controller
    op.usbcmd |= USBCMD_RUN_STOP;

    Ok(())
}

// ============ Global controller access ============

pub fn controller() -> Option<&'static XhciController> {
    XHCI.lock().as_deref()
}

pub fn controller_mut() -> Option<&'static mut XhciController> {
    XHCI.lock().as_deref_mut()
}

// ============ Command ring helpers ============

impl XhciController {
    fn doorbell_command(&mut self) {
        unsafe {
            let db0 = self.regs.doorbell_ptr(0);
            core::ptr::write_volatile(db0, 0);
        }
    }

    fn doorbell_ep(&mut self, slot_id: u8, ep_id: u8) {
        unsafe {
            let db = self.regs.doorbell_ptr(slot_id);
            let val = (ep_id as u32) & 0xFF; // target endpoint
            core::ptr::write_volatile(db, val);
        }
    }

    pub fn cmd_enable_slot(&mut self) -> Result<u8, &'static str> {
        let mut trb = Trb::default();
        trb.dword3 = trb_set_type(TRB_TYPE_ENABLE_SLOT);

        self.cmd_ring.push_trb(trb);
        self.doorbell_command();

        let c = self.wait_for_event_of_type(TRB_TYPE_COMMAND_COMPLETION_EVENT)?
            .ok_or("[xHCI] Enable Slot: no completion")?;

        let slot_id = (c.dword3 >> 24) as u8;
        if slot_id == 0 {
            return Err("[xHCI] Enable Slot returned slot_id=0");
        }
        Ok(slot_id)
    }

    /// Very simplified Address Device — does not yet build full contexts.
    pub fn cmd_address_device(
        &mut self,
        slot_id: u8,
        _port: u8,
        _speed_code: u8,
        _ep0_max_packet: u16,
    ) -> Result<(), &'static str> {
        let mut trb = Trb::default();
        trb.dword0 = 0;
        trb.dword1 = 0;
        trb.dword3 = trb_set_type(TRB_TYPE_ADDRESS_DEVICE) | ((slot_id as u32) << 24);

        self.cmd_ring.push_trb(trb);
        self.doorbell_command();

        let c = self.wait_for_event_of_type(TRB_TYPE_COMMAND_COMPLETION_EVENT)?
            .ok_or("[xHCI] Address Device: no completion")?;
        let completion_code = (c.dword2 >> 24) & 0xFF;
        if completion_code != 1 {
            return Err("[xHCI] Address Device failed");
        }
        Ok(())
    }

    /// Poll event ring until we see a TRB of the desired type, or timeout.
    fn wait_for_event_of_type(&mut self, trb_type: u32) -> Result<Option<Trb>, &'static str> {
        for _ in 0..5_000_000 {
            if let Some(trb) = self.evt_ring.ring.pop_trb() {
                let ty = (trb.dword3 >> 10) & 0x3F;
                if ty == trb_type {
                    return Ok(Some(trb));
                }
            }
        }
        Err("[xHCI] wait_for_event_of_type: timeout")
    }
}

// ============ Port enumeration ============

pub fn enumerate_ports() -> Vec<UsbDevice> {
    let mut out = Vec::new();
    let ctrl = match controller_mut() {
        Some(c) => c,
        None => {
            vga_buffer::log_line("[xHCI] enumerate_ports(): controller not initialized");
            return out;
        }
    };

    let max_ports = ctrl.max_ports;
    if max_ports == 0 {
        vga_buffer::log_line("[xHCI] enumerate_ports(): max_ports = 0");
        return out;
    }

    vga_buffer::log_line("[xHCI] Enumerating xHCI ports...");

    for i in 0..max_ports {
        let port_num = i + 1;
        let portsc_ptr = ctrl.regs.portsc_ptr(i);
        let mut portsc_val = unsafe { core::ptr::read_volatile(portsc_ptr) };

        let ccs = (portsc_val & 1) != 0;
        if !ccs {
            continue;
        }

        vga_buffer::log_line(&format!(
            "[xHCI] Port {}: device present, PORTSC=0x{:08x}",
            port_num, portsc_val
        ));

        if let Err(e) = reset_port(port_num as u8, portsc_ptr) {
            vga_buffer::log_line(e);
            continue;
        }

        portsc_val = unsafe { core::ptr::read_volatile(portsc_ptr) };
        let speed = decode_port_speed(portsc_val);
        vga_buffer::log_line(&format!(
            "[xHCI] Port {}: reset complete, speed={:?}, PORTSC=0x{:08x}",
            port_num, speed, portsc_val
        ));

        out.push(UsbDevice::from_port(port_num, speed));
    }

    if out.is_empty() {
        vga_buffer::log_line("[xHCI] No devices detected on ports.");
    }

    out
}

fn reset_port(port_num: u8, portsc_ptr: *mut u32) -> Result<(), &'static str> {
    const PORTSC_PR: u32 = 1 << 4;
    const PORTSC_CCS: u32 = 1 << 0;

    unsafe {
        let mut v = core::ptr::read_volatile(portsc_ptr);
        v |= PORTSC_PR;
        core::ptr::write_volatile(portsc_ptr, v);
    }

    vga_buffer::log_line(&format!("[xHCI] Port {}: reset requested", port_num));

    let mut tries = 0;
    loop {
        let v = unsafe { core::ptr::read_volatile(portsc_ptr) };
        let pr_set = (v & PORTSC_PR) != 0;
        let ccs = (v & PORTSC_CCS) != 0;

        if !pr_set && ccs {
            break;
        }

        tries += 1;
        if tries > 1_000_000 {
            vga_buffer::log_line(&format!(
                "[xHCI] Port {}: reset timeout, PORTSC=0x{:08x}",
                port_num, v
            ));
            return Err("[xHCI] reset_port: timeout");
        }
    }

    Ok(())
}

fn decode_port_speed(portsc: u32) -> PortSpeed {
    let speed = ((portsc >> 10) & 0x0F) as u8;
    match speed {
        1 => PortSpeed::Full,
        2 => PortSpeed::Low,
        3 => PortSpeed::High,
        4 => PortSpeed::Super,
        5 => PortSpeed::SuperPlus,
        other => PortSpeed::Unknown(other),
    }
}

// ============ Transfer rings + interrupt_in ============

/// Ensure we have a transfer ring for (slot_id, ep_id), create if missing.
fn get_or_create_ep_ring(
    ctrl: &mut XhciController,
    slot_id: u8,
    ep_id: u8,
) -> Result<&mut XhciRing, &'static str> {
    if slot_id as usize >= ctrl.ep_rings.len() {
        return Err("[xHCI] get_or_create_ep_ring: slot_id out of range");
    }
    let slot = &mut ctrl.ep_rings[slot_id as usize];
    if ep_id as usize >= slot.len() {
        return Err("[xHCI] get_or_create_ep_ring: ep_id out of range");
    }
    if slot[ep_id as usize].is_none() {
        slot[ep_id as usize] = Some(XhciRing::new(64)?);
    }
    Ok(slot[ep_id as usize].as_mut().unwrap())
}

/// Real interrupt-IN transfer using transfer ring + event ring.
///
/// NOTE:
/// - Still uses virtual addresses as DMA pointers.
/// - Assumes endpoint context is already configured (we haven't done that part yet).
pub fn interrupt_in(slot_id: u8, ep_id: u8, buf: &mut [u8]) -> Result<usize, &'static str> {
    let ctrl = controller_mut().ok_or("[xHCI] interrupt_in: no controller")?;
    let ring = get_or_create_ep_ring(ctrl, slot_id, ep_id)?;

    // Build a Normal TRB for the transfer
    let ptr = buf.as_mut_ptr() as u64;

    let mut trb = Trb::default();
    trb.dword0 = ptr as u32;
    trb.dword1 = (ptr >> 32) as u32;
    trb.dword2 = buf.len() as u32;
    trb.dword3 = trb_set_type(TRB_TYPE_NORMAL) | TRB_IOC; // interrupt on completion

    ring.push_trb(trb);

    // Ring endpoint doorbell
    ctrl.doorbell_ep(slot_id, ep_id);

    // Wait for Transfer Event
    let evt = ctrl
        .wait_for_event_of_type(TRB_TYPE_TRANSFER_EVENT)?
        .ok_or("[xHCI] interrupt_in: no transfer event")?;

    // Transfer Event TRB:
    // DW2 bits 0:23: Transfer Length (remaining).
    let remaining = (evt.dword2 & 0x00FF_FFFF) as usize;

    let requested = buf.len();
    let transferred = requested.saturating_sub(remaining);

    Ok(transferred)
}

// ============ Control transfers ============

#[derive(Clone, Copy)]
enum ControlDir {
    NoData,
    In,
    Out,
}

/// Full control-transfer over EP0:
/// SETUP → optional DATA → STATUS
///
/// - slot_id: xHCI slot for the device
/// - ep_id: usually 1 for EP0 (control endpoint)
/// - bm_request_type, b_request, w_value, w_index: standard USB setup fields
/// - data: None for no-data control, Some(&mut buf) for IN/OUT data
///
/// Returns: number of bytes transferred (for IN/OUT with data), or 0 for no-data.
pub fn control_transfer(
    slot_id: u8,
    ep_id: u8,
    bm_request_type: u8,
    b_request: u8,
    w_value: u16,
    w_index: u16,
    data: Option<&mut [u8]>,
) -> Result<usize, &'static str> {
    let ctrl = controller_mut().ok_or("[xHCI] control_transfer: no controller")?;
    let ring = get_or_create_ep_ring(ctrl, slot_id, ep_id)?;

    // Decide direction and length.
    let dir = match data {
        None => ControlDir::NoData,
        Some(buf) => {
            if buf.is_empty() {
                ControlDir::NoData
            } else if (bm_request_type & 0x80) != 0 {
                ControlDir::In
            } else {
                ControlDir::Out
            }
        }
    };

    let w_length = data.as_ref().map(|b| b.len()).unwrap_or(0) as u16;

    //
    // 1) SETUP STAGE TRB
    //
    let mut setup_trb = Trb::default();
    setup_trb.dword0 =
        (bm_request_type as u32) |
        ((b_request as u32) << 8) |
        ((w_value as u32) << 16);

    setup_trb.dword1 =
        (w_index as u32) |
        ((w_length as u32) << 16);

    setup_trb.dword2 = 8; // setup packet is always 8 bytes

    let trt = match dir {
        ControlDir::NoData => TRT_NO_DATA,
        ControlDir::Out => TRT_OUT_DATA,
        ControlDir::In => TRT_IN_DATA,
    };

    setup_trb.dword3 =
        trb_set_type(TRB_TYPE_SETUP_STAGE) |
        TRB_IDT |
        (trt << 16);

    ring.push_trb(setup_trb);

    //
    // 2) Optional DATA STAGE TRB
    //
    let mut data_trb_used = false;
    let mut data_len = 0usize;

    if let Some(buf) = data.as_deref_mut() {
        if !buf.is_empty() {
            data_trb_used = true;
            data_len = buf.len();

            let ptr = buf.as_mut_ptr() as u64;
            let mut data_trb = Trb::default();
            data_trb.dword0 = ptr as u32;
            data_trb.dword1 = (ptr >> 32) as u32;
            data_trb.dword2 = buf.len() as u32;
            data_trb.dword3 = trb_set_type(TRB_TYPE_DATA_STAGE);

            // DIR bit for data stage (bit 16): 1 = IN, 0 = OUT (for host)
            if let ControlDir::In = dir {
                data_trb.dword3 |= 1 << 16;
            }

            data_trb.dword3 |= TRB_IOC;

            ring.push_trb(data_trb);
        }
    }

    //
    // 3) STATUS STAGE TRB
    //
    let mut status_trb = Trb::default();
    status_trb.dword3 = trb_set_type(TRB_TYPE_STATUS_STAGE) | TRB_IOC;

    // Status direction is opposite of data stage when data exists.
    let status_dir_in = match dir {
        ControlDir::NoData => true,
        ControlDir::In => false,
        ControlDir::Out => true,
    };
    if status_dir_in {
        status_trb.dword3 |= 1 << 16; // DIR = IN
    }

    ring.push_trb(status_trb);

    // Ring doorbell for this endpoint.
    ctrl.doorbell_ep(slot_id, ep_id);

    //
    // 4) Wait for Transfer Event(s)
    //
    let evt = ctrl
        .wait_for_event_of_type(TRB_TYPE_TRANSFER_EVENT)?
        .ok_or("[xHCI] control_transfer: no transfer event")?;

    let remaining = (evt.dword2 & 0x00FF_FFFF) as usize;

    let transferred = match dir {
        ControlDir::NoData => 0,
        _ if data_trb_used => data_len.saturating_sub(remaining),
        _ => 0,
    };

    Ok(transferred)
}

/// Convenience wrapper: IN control transfer with data buffer.
pub fn control_in(
    slot_id: u8,
    ep_id: u8,
    bm_request_type: u8,
    b_request: u8,
    w_value: u16,
    w_index: u16,
    buf: &mut [u8],
) -> Result<usize, &'static str> {
    control_transfer(
        slot_id,
        ep_id,
        bm_request_type,
        b_request,
        w_value,
        w_index,
        Some(buf),
    )
}

/// Convenience wrapper: OUT / no-data control transfer.
pub fn control_out(
    slot_id: u8,
    ep_id: u8,
    bm_request_type: u8,
    b_request: u8,
    w_value: u16,
    w_index: u16,
    buf: Option<&mut [u8]>,
) -> Result<usize, &'static str> {
    control_transfer(
        slot_id,
        ep_id,
        bm_request_type,
        b_request,
        w_value,
        w_index,
        buf,
    )
}
