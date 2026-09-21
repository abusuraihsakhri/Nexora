pub trait LocalApic {
    fn enable(&mut self);
    fn end_of_interrupt(&mut self);
    fn set_spurious_vector(&mut self, vector: u8);
}
