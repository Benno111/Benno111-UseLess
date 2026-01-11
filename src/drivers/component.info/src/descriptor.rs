use alloc::vec::Vec;

use crate::usb::device::{
    EndpointType,
    UsbConfiguration,
    UsbEndpoint,
    UsbInterface,
    HidDescriptorInfo,
};

#[derive(Clone, Copy, Debug)]
pub struct DeviceDescriptor {
    pub usb_version_bcd: u16,
    pub device_class: u8,
    pub device_subclass: u8,
    pub device_protocol: u8,
    pub max_packet_size0: u8,
    pub vendor_id: u16,
    pub product_id: u16,
    pub device_version_bcd: u16,
    pub num_configurations: u8,
}

#[derive(Clone, Copy, Debug)]
pub struct RawConfigHeader {
    pub total_length: u16,
    pub value: u8,
    pub num_interfaces: u8,
}

/// Parse a standard USB device descriptor (18 bytes).
pub fn parse_device_descriptor(buf: &[u8]) -> Option<DeviceDescriptor> {
    if buf.len() < 18 || buf[0] != 18 || buf[1] != 0x01 {
        return None;
    }

    let usb_version_bcd = u16::from_le_bytes([buf[2], buf[3]]);
    let device_class = buf[4];
    let device_subclass = buf[5];
    let device_protocol = buf[6];
    let max_packet_size0 = buf[7];
    let vendor_id = u16::from_le_bytes([buf[8], buf[9]]);
    let product_id = u16::from_le_bytes([buf[10], buf[11]]);
    let device_version_bcd = u16::from_le_bytes([buf[12], buf[13]]);
    let num_configurations = buf[17];

    Some(DeviceDescriptor {
        usb_version_bcd,
        device_class,
        device_subclass,
        device_protocol,
        max_packet_size0,
        vendor_id,
        product_id,
        device_version_bcd,
        num_configurations,
    })
}

/// Parse just the configuration header to determine wTotalLength, etc.
pub fn parse_config_header(buf: &[u8]) -> Option<RawConfigHeader> {
    if buf.len() < 9 || buf[0] < 9 || buf[1] != 0x02 {
        return None;
    }

    let total_length = u16::from_le_bytes([buf[2], buf[3]]);
    let num_interfaces = buf[4];
    let value = buf[5];

    Some(RawConfigHeader {
        total_length,
        value,
        num_interfaces,
    })
}

/// Parse a full configuration descriptor tree (config + interfaces + endpoints).
pub fn parse_configuration_tree(buf: &[u8]) -> Option<UsbConfiguration> {
    if buf.len() < 9 || buf[1] != 0x02 {
        return None;
    }

    let header = parse_config_header(buf)?;
    let mut interfaces: Vec<UsbInterface> = Vec::new();

    let mut idx = 0;
    let mut current_if: Option<UsbInterface> = None;

    while idx + 2 <= buf.len() {
        let b_length = buf[idx];
        let b_type = buf[idx + 1];

        if b_length == 0 {
            break;
        }
        if idx + (b_length as usize) > buf.len() {
            break;
        }

        match b_type {
            0x04 => {
                // Interface descriptor
                if let Some(iface) = current_if.take() {
                    interfaces.push(iface);
                }

                if b_length < 9 {
                    idx += b_length as usize;
                    continue;
                }

                let number = buf[idx + 2];
                let alt = buf[idx + 3];
                let class = buf[idx + 5];
                let subclass = buf[idx + 6];
                let protocol = buf[idx + 7];

                current_if = Some(UsbInterface {
                    number,
                    alternate_setting: alt,
                    interface_class: class,
                    interface_subclass: subclass,
                    interface_protocol: protocol,
                    endpoints: Vec::new(),
                    hid: None,
                });
            }
            0x05 => {
                // Endpoint descriptor
                if b_length < 7 {
                    idx += b_length as usize;
                    continue;
                }

                let addr = buf[idx + 2];
                let attributes = buf[idx + 3];
                let max_packet = u16::from_le_bytes([buf[idx + 4], buf[idx + 5]]);
                let interval = buf[idx + 6];

                let ep_type = match attributes & 0x03 {
                    0 => EndpointType::Control,
                    1 => EndpointType::Isochronous,
                    2 => EndpointType::Bulk,
                    3 => EndpointType::Interrupt,
                    other => EndpointType::Unknown(other),
                };

                let ep = UsbEndpoint {
                    address: addr,
                    ep_type,
                    max_packet_size: max_packet,
                    interval,
                };

                if let Some(ref mut iface) = current_if {
                    iface.endpoints.push(ep);
                }
            }
            _ => {
                // ignore others for now
            }
        }

        idx += b_length as usize;
    }

    if let Some(iface) = current_if.take() {
        interfaces.push(iface);
    }

    Some(UsbConfiguration {
        value: header.value,
        interfaces,
    })
}

/// Parse a HID class descriptor (0x21).
///
/// Layout:
///   bLength
///   bDescriptorType (0x21)
///   bcdHID (2 bytes)
///   bCountryCode
///   bNumDescriptors
///   then bNumDescriptors * (bDescriptorType, wDescriptorLength)
pub fn parse_hid_descriptor(buf: &[u8]) -> Option<HidDescriptorInfo> {
    if buf.len() < 6 || buf[1] != 0x21 {
        return None;
    }

    let hid_version = u16::from_le_bytes([buf[2], buf[3]]);
    let country_code = buf[4];
    let num_desc = buf[5];

    let mut report_len: u16 = 0;

    // Each descriptor entry: type (1 byte), length (2 bytes)
    let mut idx = 6;
    for _ in 0..num_desc {
        if idx + 3 > buf.len() {
            break;
        }
        let dtype = buf[idx]; // e.g. 0x22 = Report
        let dlen = u16::from_le_bytes([buf[idx + 1], buf[idx + 2]]);
        idx += 3;

        if dtype == 0x22 {
            report_len = dlen;
            break;
        }
    }

    Some(HidDescriptorInfo {
        hid_version_bcd: hid_version,
        country_code,
        num_descriptors: num_desc,
        report_desc_len: report_len,
    })
}

/// Attach HID class descriptor info (0x21) to interfaces in a configuration.
///
/// `raw` must be the raw configuration descriptor buffer as received
/// from GET_DESCRIPTOR(CONFIGURATION).
pub fn attach_hid_info(config: &mut UsbConfiguration, raw: &[u8]) {
    let mut idx = 0usize;
    let mut current_if: Option<(u8, u8)> = None; // (interface_number, alt_setting)

    while idx + 2 <= raw.len() {
        let b_length = raw[idx];
        let b_type = raw[idx + 1];

        if b_length == 0 {
            break;
        }
        if idx + (b_length as usize) > raw.len() {
            break;
        }

        match b_type {
            0x04 => {
                // Interface descriptor
                if b_length >= 9 {
                    let number = raw[idx + 2];
                    let alt = raw[idx + 3];
                    current_if = Some((number, alt));
                } else {
                    current_if = None;
                }
            }
            0x21 => {
                // HID class descriptor
                if let Some((num, alt)) = current_if {
                    if let Some(hid) = parse_hid_descriptor(&raw[idx..idx + (b_length as usize)]) {
                        // Find matching UsbInterface and attach.
                        for iface in &mut config.interfaces {
                            if iface.number == num && iface.alternate_setting == alt {
                                iface.hid = Some(hid);
                                break;
                            }
                        }
                    }
                }
            }
            _ => {
                // ignore other descriptors here
            }
        }

        idx += b_length as usize;
    }
}
