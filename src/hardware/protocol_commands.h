/**
 * @file protocol_commands.h
 * @brief NETA DRX9000 console commands, message templates, and expected response patterns.
 * @author Semi Eren Gökyıldız
 * @copyright Copyright (C) 2026 NETA Elektronik A.Ş.
 */

#ifndef PROTOCOL_COMMANDS_H
#define PROTOCOL_COMMANDS_H

#include <QString>

namespace NetaProtocol {

    // 1. Packet Framing
    inline QString HEADER             = "";          
    inline QString FOOTER             = "\n";        

    // 2. Base System Commands
    inline QString CMD_HELP           = "help\n";
    inline QString CMD_GET_STATUS     = "getstatus\n";
    inline QString CMD_TEST           = "test\n";
    inline QString CMD_TRTCFG         = "trtcfg\n";
    inline QString CMD_TRTPAR         = "trtpar\n";
    inline QString CMD_MAC            = "mac\n";
    inline QString CMD_CONSOLE        = "console\n";
    inline QString CMD_REBOOT         = "reboot\n";

    // 3. Telemetry and Logging Control Commands
    inline QString CMD_START_PLOT     = "tunerlog on\n";
    inline QString CMD_STOP_PLOT      = "tunerlog off\n";

    // 4. RF Tuner Configuration Command Templates
    // Format: tune <Frequency> <SymbolRate> <Polarization> (e.g., tune 12015 27500 HOR)
    inline QString CMD_TUNE           = "tune %1 %2 %3\n";              
    // Format: setdiseqc <Port> (e.g., setdiseqc A)
    inline QString CMD_SET_DISEQC     = "setdiseqc %1\n";               
    // Format: setlnb <Type> <LowLO> <HighLO> (e.g., setlnb UNIVERSAL 9750 10600)
    inline QString CMD_SET_LNB        = "setlnb %1 %2 %3\n";            
    // Format: listchannels
    inline QString CMD_LIST_CHANNELS  = "listchannels\n";
    // Format: selectchannel <ServiceID> (e.g., selectchannel 10101)
    inline QString CMD_SELECT_CHANNEL = "selectchannel %1\n";           
    // Format: setpid <VPID> <VCodec> <APID> <ACodec> <PCRPID>
    inline QString CMD_SET_PID        = "setpid %1 %2 %3 %4 %5\n";      
    // Format: rds <State> <PID>
    inline QString CMD_SET_RDS        = "rds %1 %2\n";                  

    // 5. Automated Sequence Commands
    inline QString CMD_TEST_PING      = "getstatus\n";
    inline QString CMD_TEST_GET_ID    = "getstatus\n";
    inline QString CMD_TEST_GET_VOLT  = "getstatus\n";
    inline QString CMD_TEST_GET_SNR   = "getstatus\n";
    inline QString CMD_TEST_WRITE_NVM = "getstatus\n";

    // 6. Expected Device Response Patterns
    inline QString RESP_OK            = ":ok";
    inline QString RESP_ACK           = "ACK";
    inline QString RESP_ERROR         = ":error";
    inline QString RESP_STATUS_PREFIX = "getstatus:";
    inline QString RESP_CHANNELS_PREFIX = "listchannels:";

} // namespace NetaProtocol

#endif // PROTOCOL_COMMANDS_H
