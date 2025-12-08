use alloc::vec::Vec;

#[derive(Clone, Copy, Debug)]
pub enum PortSpeed {
    Low,
    Full,
    High,
    Super,
    SuperPlus,
    Unknown(u8),
}

#[derive(Clone, Copy, Debug)]
pub enum EndpointType {
    Control,
    Isochronous,
    Bulk,
    Interrupt,
    Unknown(u8),
}

#[derive(Clone, Copy, Debug)]
pub struct UsbEndpoint {
    pub address: u8,         // includes direction bit 7
    pub ep_type: EndpointType,
    pub max_packet_size: u16,
    pub interval: u8,
}

/// Parsed HID class descriptor (0x21) for an interface.
#[derive(Clone, Copy, Debug)]
pub struct HidDescriptorInfo {
    pub hid_version_bcd: u16,
    pub country_code: u8,
    pub num_descriptors: u8,
    /// Length of the first report descriptor in bytes (if present).
    pub report_desc_len: u16,
}

#[derive(Clone, Debug)]
pub struct UsbInterface {
    pub number: u8,
    pub alternate_setting: u8,
    pub interface_class: u8,
    pub interface_subclass: u8,
    pub interface_protocol: u8,
    pub endpoints: Vec<UsbEndpoint>,

    /// Optional HID descriptor for this interface (if HID class).
    pub hid: Option<HidDescriptorInfo>,
}

#[derive(Clone, Debug)]
pub struct UsbConfiguration {
    pub value: u8,
    pub interfaces: Vec<UsbInterface>,
}

#[derive(Clone, Debug)]
pub struct UsbDevice {
    /// 1-based xHCI root port number.
    pub port: u8,
    pub speed: PortSpeed,

    /// xHCI slot ID (not the USB bus address).
    pub slot_id: u8,

    /// Logical USB address (0 if not used / unknown).
    pub address: u8,

    pub vendor_id: u16,
    pub product_id: u16,
    pub device_class: u8,
    pub device_subclass: u8,
    pub device_protocol: u8,
    pub max_packet_size0: u8,

    pub configurations: Vec<UsbConfiguration>,
}

impl UsbDevice {
    pub fn from_port(port: u8, speed: PortSpeed) -> Self {
        UsbDevice {
            port,
            speed,
            slot_id: 0,
            address: 0,
            vendor_id: 0,
            product_id: 0,
            device_class: 0,
            device_subclass: 0,
            device_protocol: 0,
            max_packet_size0: 8,
            configurations: Vec::new(),
        }
    }
}
