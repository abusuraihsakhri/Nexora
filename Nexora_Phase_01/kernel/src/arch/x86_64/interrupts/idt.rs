#[repr(C, packed)]
#[derive(Clone, Copy)]
pub struct IdtEntry {
    pub offset_low: u16,
    pub selector: u16,
    pub ist: u8,
    pub attributes: u8,
    pub offset_mid: u16,
    pub offset_high: u32,
    pub reserved: u32,
}

#[repr(C, align(16))]
pub struct InterruptDescriptorTable {
    pub entries: [IdtEntry; 256],
}
