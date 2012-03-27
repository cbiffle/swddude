#include "swd_dp.h"

#include "swd.h"

using Err::Error;

/*******************************************************************************
 * DebugAccessPort private implementation
 */

Error DebugAccessPort::select_ap_bank(uint8_t ap, uint8_t address)
{
    ARM::word_t sel = (ap << 24) | (address & 0xF0) | (_SELECT & 1);

    if (sel != _SELECT) {
        Check(write_select(sel));
    }

    return Err::success;
}


/*******************************************************************************
 * DebugAccessPort public implementation
 */

DebugAccessPort::DebugAccessPort(SWDDriver & swd) :
    _swd(swd),
    _SELECT(-1) {}

Error DebugAccessPort::reset_state()
{
    Check(write_select(0));  // Reset SELECT and cache.
    Check(write_abort((1 << 1)  // Clear STKCMP
                    | (1 << 2)  // Clear STKERR
                    | (1 << 3)  // Clear WDERR
                    | (1 << 4)  // Clear ORUNERR
                    ));
    Check(write_ctrlstat((1 << 30)     // CSYSPWRUPREQ
                       | (1 << 28)));  // CDBGPWRUPREQ
    return Err::success;
}

Error DebugAccessPort::read_idcode(ARM::word_t * data)
{
    return _swd.read(kRegIDCODE, true, data);
}

Error DebugAccessPort::write_abort(ARM::word_t data)
{
    return _swd.write(kRegABORT, true, data);
}

Error DebugAccessPort::read_ctrlstat(ARM::word_t * data)
{
    if (_SELECT & 1) Check(write_select(_SELECT & ~1));

    return _swd.read(kRegCTRLSTAT, true, data);
}

Error DebugAccessPort::write_ctrlstat(ARM::word_t data)
{
    if (_SELECT & 1) Check(write_select(_SELECT & ~1));

    return _swd.write(kRegCTRLSTAT, true, data);
}

Error DebugAccessPort::write_select(ARM::word_t data)
{
    Check(_swd.write(kRegSELECT, true, data));

    _SELECT = data;
    return Err::success;
}

Error DebugAccessPort::read_resend(ARM::word_t * data)
{
    return _swd.read(kRegRESEND, true, data);
}

Error DebugAccessPort::read_rdbuff(ARM::word_t * data)
{
    return _swd.read(kRegRDBUFF, true, data);
}

Error DebugAccessPort::start_read_ap(uint8_t ap_index, uint8_t address)
{
    if (address & 3) return Err::argument_error;

    Check(select_ap_bank(ap_index, address));

    return _swd.read((address >> 2) & 3, false, 0);
}

Error DebugAccessPort::step_read_ap(uint8_t ap_index,
                                    uint8_t address,
                                    ARM::word_t * last)
{
    if (address & 3) return Err::argument_error;

    Check(select_ap_bank(ap_index, address));

    return _swd.read((address >> 2) & 3, false, last);
}

Error DebugAccessPort::write_ap(uint8_t ap_index,
                                uint8_t address,
                                ARM::word_t data)
{
    if (address & 3) return Err::argument_error;

    Check(select_ap_bank(ap_index, address));

    return _swd.write((address >> 2) & 3, false, data);
}


