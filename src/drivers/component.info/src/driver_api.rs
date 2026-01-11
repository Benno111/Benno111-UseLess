use crate::driver_ring::{DriverRequest, DriverResponse, DriverStats, DriverInputFrame};

pub fn init() {
    let _ = crate::driver_ring::handle_request(DriverRequest::Init);
}

pub fn poll_devices() -> DriverInputFrame {
    match crate::driver_ring::handle_request(DriverRequest::PollInput) {
        DriverResponse::Input(frame) => frame,
        _ => DriverInputFrame::new(),
    }
}

pub fn stats() -> DriverStats {
    match crate::driver_ring::handle_request(DriverRequest::QueryStats) {
        DriverResponse::Stats(stats) => stats,
        _ => DriverStats {
            poll_skips: 0,
            poll_budget: 0,
        },
    }
}
