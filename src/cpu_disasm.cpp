#include "cpu.h"
#include "YBaseLib/String.h"
#include "YBaseLib/NumericLimits.h"
#include "YBaseLib/Log.h"
Log_SetChannel(CPU);

void CPU::DisassembleFrom(System *system, uint16 address, uint16 count)
{
    uint16 start_address = address;
    uint16 end_address;
    if ((uint32)start_address + count > Y_UINT16_MAX)
        end_address = Y_UINT16_MAX;
    else
        end_address = start_address + count;

    for (uint16 current_address = start_address; current_address < end_address; )
    {
        SmallString str;
        if (!Disassemble(&str, system, current_address))
        {
            Log_ErrorPrintf("Disasm fail at %04X", address);
            return;
        }

        const Instruction *instruction = &instructions[system->CPURead(current_address)];
        if (instruction->type == Instruction::Type_Prefix)
            instruction = &cb_instructions[system->CPURead(current_address + 1)];

        current_address += (uint16)instruction->length;
        Log_DevPrint(str);
    }
}

bool CPU::Disassemble(String *pDestination, System *memory, uint16 address)
{
    // read first byte
    uint8 instr = memory->CPURead(address);
    uint8 imm8 = memory->CPURead(address + 1);
    uint8 imm16b1 = imm8;
    uint8 imm16b2 = memory->CPURead(address + 2);
    uint16 imm16 = (uint16)imm16b1 | ((uint16)imm16b2 << 8);

    // ugly as hell
    switch (instr)
    {
    case 0x00:
        pDestination->Format("%04X 00       NOP", address);
        return true;

    case 0x01:
        pDestination->Format("%04X 01       LD BC,$%04X", address, imm16);
        return true;

    case 0x02:
        pDestination->Format("%04X 02       LD (BC),A", address);
        return true;

    case 0x03:
        pDestination->Format("%04X 03       INC BC", address);
        return true;

    case 0x04:
        pDestination->Format("%04X 04       INC B", address);
        return true;

    case 0x05:
        pDestination->Format("%04X 05       DEC B", address);
        return true;

    case 0x06:
        pDestination->Format("%04X 06       LD B,n", address);
        return true;

    case 0x07:
        pDestination->Format("%04X 07       RLC A", address);
        return true;

    case 0x08:
        pDestination->Format("%04X 08 %02X %02X LD ($%04Xh), SP", address, imm16b1, imm16b2, imm16);
        return true;

    case 0x09:
        pDestination->Format("%04X 09       ADD HL,BC", address);
        return true;

    case 0x0A:
        pDestination->Format("%04X 0A       LD A,(BC)", address);
        return true;

    case 0x0B:
        pDestination->Format("%04X 0B       DEC BC", address);
        return true;

    case 0x0C:
        pDestination->Format("%04X 0C       INC C", address);
        return true;

    case 0x0D:
        pDestination->Format("%04X 0D       DEC C", address);
        return true;

    case 0x0E:
        pDestination->Format("%04X 0E %02X    LD C, $%02X", address, imm8, imm8);
        return true;

    case 0x0F:
        pDestination->Format("%04X 0F       RRC A", address);
        return true;

    case 0x10:
        pDestination->Format("%04X 10       STOP", address);
        return true;

    case 0x11:
        pDestination->Format("%04X 11       LD DE, $%04X", address, imm16);
        return true;

    case 0x12:
        pDestination->Format("%04X 12       LD (DE),A", address);
        return true;

    case 0x13:
        pDestination->Format("%04X 13       INC DE", address);
        return true;

    case 0x14:
        pDestination->Format("%04X 14       INC D", address);
        return true;

    case 0x15:
        pDestination->Format("%04X 15       DEC D", address);
        return true;

    case 0x16:
        pDestination->Format("%04X 16       LD D,n", address);
        return true;

    case 0x17:
        pDestination->Format("%04X 17       RLA", address);
        return true;

    case 0x18:
        pDestination->Format("%04X 18       JR n", address);
        return true;

    case 0x19:
        pDestination->Format("%04X 19       ADD HL,DE", address);
        return true;

    case 0x1A:
        pDestination->Format("%04X 1A       LD A,(DE)", address);
        return true;

    case 0x1B:
        pDestination->Format("%04X 1B       DEC DE", address);
        return true;

    case 0x1C:
        pDestination->Format("%04X 1C       INC E", address);
        return true;

    case 0x1D:
        pDestination->Format("%04X 1D       DEC E", address);
        return true;

    case 0x1E:
        pDestination->Format("%04X 1E       LD E,n", address);
        return true;

    case 0x1F:
        pDestination->Format("%04X 1F       RR A", address);
        return true;

    case 0x20:
        pDestination->Format("%04X 20 %02X    JR NZ, %i", address, imm8, (int8)imm8);
        return true;

    case 0x21:
        pDestination->Format("%04X 21 %02X %02X LD HL,$%04X", address, imm16b1, imm16b2, imm16);
        return true;

    case 0x22:
        pDestination->Format("%04X 22       LDI (HL),A", address);
        return true;

    case 0x23:
        pDestination->Format("%04X 23       INC HL", address);
        return true;

    case 0x24:
        pDestination->Format("%04X 24       INC H", address);
        return true;

    case 0x25:
        pDestination->Format("%04X 25       DEC H", address);
        return true;

    case 0x26:
        pDestination->Format("%04X 26       LD H,n", address);
        return true;

    case 0x27:
        pDestination->Format("%04X 27       DAA", address);
        return true;

    case 0x28:
        pDestination->Format("%04X 28       JR Z,n", address);
        return true;

    case 0x29:
        pDestination->Format("%04X 29       ADD HL,HL", address);
        return true;

    case 0x2A:
        pDestination->Format("%04X 2A       LDI A,(HL)", address);
        return true;

    case 0x2B:
        pDestination->Format("%04X 2B       DEC HL", address);
        return true;

    case 0x2C:
        pDestination->Format("%04X 2C       INC L", address);
        return true;

    case 0x2D:
        pDestination->Format("%04X 2D       DEC L", address);
        return true;

    case 0x2E:
        pDestination->Format("%04X 2E       LD L,n", address);
        return true;

    case 0x2F:
        pDestination->Format("%04X 2F       CPL", address);
        return true;

    case 0x30:
        pDestination->Format("%04X 30       JR NC,n", address);
        return true;

    case 0x31:
        pDestination->Format("%04X 31 %02X %02X LD SP, %04X", address, imm16b1, imm16b2, imm16);
        return true;

    case 0x32:
        pDestination->Format("%04X 32       LDD (HL),A", address);
        return true;

    case 0x33:
        pDestination->Format("%04X 33       INC SP", address);
        return true;

    case 0x34:
        pDestination->Format("%04X 34       INC (HL)", address);
        return true;

    case 0x35:
        pDestination->Format("%04X 35       DEC (HL)", address);
        return true;

    case 0x36:
        pDestination->Format("%04X 36       LD (HL),n", address);
        return true;

    case 0x37:
        pDestination->Format("%04X 37       SCF", address);
        return true;

    case 0x38:
        pDestination->Format("%04X 38       JR C,n", address);
        return true;

    case 0x39:
        pDestination->Format("%04X 39       ADD HL,SP", address);
        return true;

    case 0x3A:
        pDestination->Format("%04X 3A       LDD A,(HL)", address);
        return true;

    case 0x3B:
        pDestination->Format("%04X 3B       DEC SP", address);
        return true;

    case 0x3C:
        pDestination->Format("%04X 3C       INC A", address);
        return true;

    case 0x3D:
        pDestination->Format("%04X 3D       DEC A", address);
        return true;

    case 0x3E:
        pDestination->Format("%04X 3E %02X    LD A, %u", address, imm8, imm8);
        return true;

    case 0x3F:
        pDestination->Format("%04X 3F       CCF", address);
        return true;

    case 0x40:
        pDestination->Format("%04X 40       LD B,B", address);
        return true;

    case 0x41:
        pDestination->Format("%04X 41       LD B,C", address);
        return true;

    case 0x42:
        pDestination->Format("%04X 42       LD B,D", address);
        return true;

    case 0x43:
        pDestination->Format("%04X 43       LD B,E", address);
        return true;

    case 0x44:
        pDestination->Format("%04X 44       LD B,H", address);
        return true;

    case 0x45:
        pDestination->Format("%04X 45       LD B,L", address);
        return true;

    case 0x46:
        pDestination->Format("%04X 46       LD B,(HL)", address);
        return true;

    case 0x47:
        pDestination->Format("%04X 47       LD B,A", address);
        return true;

    case 0x48:
        pDestination->Format("%04X 48       LD C,B", address);
        return true;

    case 0x49:
        pDestination->Format("%04X 49       LD C,C", address);
        return true;

    case 0x4A:
        pDestination->Format("%04X 4A       LD C,D", address);
        return true;

    case 0x4B:
        pDestination->Format("%04X 4B       LD C,E", address);
        return true;

    case 0x4C:
        pDestination->Format("%04X 4C       LD C,H", address);
        return true;

    case 0x4D:
        pDestination->Format("%04X 4D       LD C,L", address);
        return true;

    case 0x4E:
        pDestination->Format("%04X 4E       LD C,(HL)", address);
        return true;

    case 0x4F:
        pDestination->Format("%04X 4F       LD C,A", address);
        return true;

    case 0x50:
        pDestination->Format("%04X 50       LD D,B", address);
        return true;

    case 0x51:
        pDestination->Format("%04X 51       LD D,C", address);
        return true;

    case 0x52:
        pDestination->Format("%04X 52       LD D,D", address);
        return true;

    case 0x53:
        pDestination->Format("%04X 53       LD D,E", address);
        return true;

    case 0x54:
        pDestination->Format("%04X 54       LD D,H", address);
        return true;

    case 0x55:
        pDestination->Format("%04X 55       LD D,L", address);
        return true;

    case 0x56:
        pDestination->Format("%04X 56       LD D,(HL)", address);
        return true;

    case 0x57:
        pDestination->Format("%04X 57       LD D,A", address);
        return true;

    case 0x58:
        pDestination->Format("%04X 58       LD E,B", address);
        return true;

    case 0x59:
        pDestination->Format("%04X 59       LD E,C", address);
        return true;

    case 0x5A:
        pDestination->Format("%04X 5A       LD E,D", address);
        return true;

    case 0x5B:
        pDestination->Format("%04X 5B       LD E,E", address);
        return true;

    case 0x5C:
        pDestination->Format("%04X 5C       LD E,H", address);
        return true;

    case 0x5D:
        pDestination->Format("%04X 5D       LD E,L", address);
        return true;

    case 0x5E:
        pDestination->Format("%04X 5E       LD E,(HL)", address);
        return true;

    case 0x5F:
        pDestination->Format("%04X 5F       LD E,A", address);
        return true;

    case 0x60:
        pDestination->Format("%04X 60       LD H,B", address);
        return true;

    case 0x61:
        pDestination->Format("%04X 61       LD H,C", address);
        return true;

    case 0x62:
        pDestination->Format("%04X 62       LD H,D", address);
        return true;

    case 0x63:
        pDestination->Format("%04X 63       LD H,E", address);
        return true;

    case 0x64:
        pDestination->Format("%04X 64       LD H,H", address);
        return true;

    case 0x65:
        pDestination->Format("%04X 65       LD H,L", address);
        return true;

    case 0x66:
        pDestination->Format("%04X 66       LD H,(HL)", address);
        return true;

    case 0x67:
        pDestination->Format("%04X 67       LD H,A", address);
        return true;

    case 0x68:
        pDestination->Format("%04X 68       LD L,B", address);
        return true;

    case 0x69:
        pDestination->Format("%04X 69       LD L,C", address);
        return true;

    case 0x6A:
        pDestination->Format("%04X 6A       LD L,D", address);
        return true;

    case 0x6B:
        pDestination->Format("%04X 6B       LD L,E", address);
        return true;

    case 0x6C:
        pDestination->Format("%04X 6C       LD L,H", address);
        return true;

    case 0x6D:
        pDestination->Format("%04X 6D       LD L,L", address);
        return true;

    case 0x6E:
        pDestination->Format("%04X 6E       LD L,(HL)", address);
        return true;

    case 0x6F:
        pDestination->Format("%04X 6F       LD L,A", address);
        return true;

    case 0x70:
        pDestination->Format("%04X 70       LD (HL),B", address);
        return true;

    case 0x71:
        pDestination->Format("%04X 71       LD (HL),C", address);
        return true;

    case 0x72:
        pDestination->Format("%04X 72       LD (HL),D", address);
        return true;

    case 0x73:
        pDestination->Format("%04X 73       LD (HL),E", address);
        return true;

    case 0x74:
        pDestination->Format("%04X 74       LD (HL),H", address);
        return true;

    case 0x75:
        pDestination->Format("%04X 75       LD (HL),L", address);
        return true;

    case 0x76:
        pDestination->Format("%04X 76       HALT", address);
        return true;

    case 0x77:
        pDestination->Format("%04X 77       LD (HL),A", address);
        return true;

    case 0x78:
        pDestination->Format("%04X 78       LD A,B", address);
        return true;

    case 0x79:
        pDestination->Format("%04X 79       LD A,C", address);
        return true;

    case 0x7A:
        pDestination->Format("%04X 7A       LD A,D", address);
        return true;

    case 0x7B:
        pDestination->Format("%04X 7B       LD A,E", address);
        return true;

    case 0x7C:
        pDestination->Format("%04X 7C       LD A,H", address);
        return true;

    case 0x7D:
        pDestination->Format("%04X 7D       LD A,L", address);
        return true;

    case 0x7E:
        pDestination->Format("%04X 7E       LD A,(HL)", address);
        return true;

    case 0x7F:
        pDestination->Format("%04X 7F       LD A,A", address);
        return true;

    case 0x80:
        pDestination->Format("%04X 80       ADD A,B", address);
        return true;

    case 0x81:
        pDestination->Format("%04X 81       ADD A,C", address);
        return true;

    case 0x82:
        pDestination->Format("%04X 82       ADD A,D", address);
        return true;

    case 0x83:
        pDestination->Format("%04X 83       ADD A,E", address);
        return true;

    case 0x84:
        pDestination->Format("%04X 84       ADD A,H", address);
        return true;

    case 0x85:
        pDestination->Format("%04X 85       ADD A,L", address);
        return true;

    case 0x86:
        pDestination->Format("%04X 86       ADD A,(HL)", address);
        return true;

    case 0x87:
        pDestination->Format("%04X 87       ADD A,A", address);
        return true;

    case 0x88:
        pDestination->Format("%04X 88       ADC A,B", address);
        return true;

    case 0x89:
        pDestination->Format("%04X 89       ADC A,C", address);
        return true;

    case 0x8A:
        pDestination->Format("%04X 8A       ADC A,D", address);
        return true;

    case 0x8B:
        pDestination->Format("%04X 8B       ADC A,E", address);
        return true;

    case 0x8C:
        pDestination->Format("%04X 8C       ADC A,H", address);
        return true;

    case 0x8D:
        pDestination->Format("%04X 8D       ADC A,L", address);
        return true;

    case 0x8E:
        pDestination->Format("%04X 8E       ADC A,(HL)", address);
        return true;

    case 0x8F:
        pDestination->Format("%04X 8F       ADC A,A", address);
        return true;

    case 0x90:
        pDestination->Format("%04X 90       SUB A,B", address);
        return true;

    case 0x91:
        pDestination->Format("%04X 91       SUB A,C", address);
        return true;

    case 0x92:
        pDestination->Format("%04X 92       SUB A,D", address);
        return true;

    case 0x93:
        pDestination->Format("%04X 93       SUB A,E", address);
        return true;

    case 0x94:
        pDestination->Format("%04X 94       SUB A,H", address);
        return true;

    case 0x95:
        pDestination->Format("%04X 95       SUB A,L", address);
        return true;

    case 0x96:
        pDestination->Format("%04X 96       SUB A,(HL)", address);
        return true;

    case 0x97:
        pDestination->Format("%04X 97       SUB A,A", address);
        return true;

    case 0x98:
        pDestination->Format("%04X 98       SBC A,B", address);
        return true;

    case 0x99:
        pDestination->Format("%04X 99       SBC A,C", address);
        return true;

    case 0x9A:
        pDestination->Format("%04X 9A       SBC A,D", address);
        return true;

    case 0x9B:
        pDestination->Format("%04X 9B       SBC A,E", address);
        return true;

    case 0x9C:
        pDestination->Format("%04X 9C       SBC A,H", address);
        return true;

    case 0x9D:
        pDestination->Format("%04X 9D       SBC A,L", address);
        return true;

    case 0x9E:
        pDestination->Format("%04X 9E       SBC A,(HL)", address);
        return true;

    case 0x9F:
        pDestination->Format("%04X 9F       SBC A,A", address);
        return true;

    case 0xA0:
        pDestination->Format("%04X A0       AND B", address);
        return true;

    case 0xA1:
        pDestination->Format("%04X A1       AND C", address);
        return true;

    case 0xA2:
        pDestination->Format("%04X A2       AND D", address);
        return true;

    case 0xA3:
        pDestination->Format("%04X A3       AND E", address);
        return true;

    case 0xA4:
        pDestination->Format("%04X A4       AND H", address);
        return true;

    case 0xA5:
        pDestination->Format("%04X A5       AND L", address);
        return true;

    case 0xA6:
        pDestination->Format("%04X A6       AND (HL)", address);
        return true;

    case 0xA7:
        pDestination->Format("%04X A7       AND A", address);
        return true;

    case 0xA8:
        pDestination->Format("%04X A8       XOR B", address);
        return true;

    case 0xA9:
        pDestination->Format("%04X A9       XOR C", address);
        return true;

    case 0xAA:
        pDestination->Format("%04X AA       XOR D", address);
        return true;

    case 0xAB:
        pDestination->Format("%04X AB       XOR E", address);
        return true;

    case 0xAC:
        pDestination->Format("%04X AC       XOR H", address);
        return true;

    case 0xAD:
        pDestination->Format("%04X AD       XOR L", address);
        return true;

    case 0xAE:
        pDestination->Format("%04X AE       XOR (HL)", address);
        return true;

    case 0xAF:
        pDestination->Format("%04X AF       XOR A", address);
        return true;

    case 0xB0:
        pDestination->Format("%04X B0       OR B", address);
        return true;

    case 0xB1:
        pDestination->Format("%04X B1       OR C", address);
        return true;

    case 0xB2:
        pDestination->Format("%04X B2       OR D", address);
        return true;

    case 0xB3:
        pDestination->Format("%04X B3       OR E", address);
        return true;

    case 0xB4:
        pDestination->Format("%04X B4       OR H", address);
        return true;

    case 0xB5:
        pDestination->Format("%04X B5       OR L", address);
        return true;

    case 0xB6:
        pDestination->Format("%04X B6       OR (HL)", address);
        return true;

    case 0xB7:
        pDestination->Format("%04X B7       OR A", address);
        return true;

    case 0xB8:
        pDestination->Format("%04X B8       CP B", address);
        return true;

    case 0xB9:
        pDestination->Format("%04X B9       CP C", address);
        return true;

    case 0xBA:
        pDestination->Format("%04X BA       CP D", address);
        return true;

    case 0xBB:
        pDestination->Format("%04X BB       CP E", address);
        return true;

    case 0xBC:
        pDestination->Format("%04X BC       CP H", address);
        return true;

    case 0xBD:
        pDestination->Format("%04X BD       CP L", address);
        return true;

    case 0xBE:
        pDestination->Format("%04X BE       CP (HL)", address);
        return true;

    case 0xBF:
        pDestination->Format("%04X BF       CP A", address);
        return true;

    case 0xC0:
        pDestination->Format("%04X C0       RET NZ", address);
        return true;

    case 0xC1:
        pDestination->Format("%04X C1       POP BC", address);
        return true;

    case 0xC2:
        pDestination->Format("%04X C2       JP NZ,$%04X", address, imm16);
        return true;

    case 0xC3:
        pDestination->Format("%04X C3       JP nn", address);
        return true;

    case 0xC4:
        pDestination->Format("%04X C4       CALL NZ,$%04X", address, imm16);
        return true;

    case 0xC5:
        pDestination->Format("%04X C5       PUSH BC", address);
        return true;

    case 0xC6:
        pDestination->Format("%04X C6       ADD A,n", address);
        return true;

    case 0xC7:
        pDestination->Format("%04X C7       RST 0", address);
        return true;

    case 0xC8:
        pDestination->Format("%04X C8       RET Z", address);
        return true;

    case 0xC9:
        pDestination->Format("%04X C9       RET", address);
        return true;

    case 0xCA:
        pDestination->Format("%04X CA       JP Z,nn", address);
        return true;

    case 0xCB:
        {
            uint8 instr2 = imm8;
            switch (instr2)
            {
            case 0x00:
                pDestination->Format("%04X CB 00    RLC B", address);
                return true;

            case 0x01:
                pDestination->Format("%04X CB 01    RLC C", address);
                return true;

            case 0x02:
                pDestination->Format("%04X CB 02    RLC D", address);
                return true;

            case 0x03:
                pDestination->Format("%04X CB 03    RLC E", address);
                return true;

            case 0x04:
                pDestination->Format("%04X CB 04    RLC H", address);
                return true;

            case 0x05:
                pDestination->Format("%04X CB 05    RLC L", address);
                return true;

            case 0x06:
                pDestination->Format("%04X CB 06    RLC (HL)", address);
                return true;

            case 0x07:
                pDestination->Format("%04X CB 07    RLC A", address);
                return true;

            case 0x08:
                pDestination->Format("%04X CB 08    RRC B", address);
                return true;

            case 0x09:
                pDestination->Format("%04X CB 09    RRC C", address);
                return true;

            case 0x0A:
                pDestination->Format("%04X CB 0A    RRC D", address);
                return true;

            case 0x0B:
                pDestination->Format("%04X CB 0B    RRC E", address);
                return true;

            case 0x0C:
                pDestination->Format("%04X CB 0C    RRC H", address);
                return true;

            case 0x0D:
                pDestination->Format("%04X CB 0D    RRC L", address);
                return true;

            case 0x0E:
                pDestination->Format("%04X CB 0E    RRC (HL)", address);
                return true;

            case 0x0F:
                pDestination->Format("%04X CB 0F    RRC A", address);
                return true;

            case 0x10:
                pDestination->Format("%04X CB 10    RL B", address);
                return true;

            case 0x11:
                pDestination->Format("%04X CB 11    RL C", address);
                return true;

            case 0x12:
                pDestination->Format("%04X CB 12    RL D", address);
                return true;

            case 0x13:
                pDestination->Format("%04X CB 13    RL E", address);
                return true;

            case 0x14:
                pDestination->Format("%04X CB 14    RL H", address);
                return true;

            case 0x15:
                pDestination->Format("%04X CB 15    RL L", address);
                return true;

            case 0x16:
                pDestination->Format("%04X CB 16    RL (HL)", address);
                return true;

            case 0x17:
                pDestination->Format("%04X CB 17    RL A", address);
                return true;

            case 0x18:
                pDestination->Format("%04X CB 18    RR B", address);
                return true;

            case 0x19:
                pDestination->Format("%04X CB 19    RR C", address);
                return true;

            case 0x1A:
                pDestination->Format("%04X CB 1A    RR D", address);
                return true;

            case 0x1B:
                pDestination->Format("%04X CB 1B    RR E", address);
                return true;

            case 0x1C:
                pDestination->Format("%04X CB 1C    RR H", address);
                return true;

            case 0x1D:
                pDestination->Format("%04X CB 1D    RR L", address);
                return true;

            case 0x1E:
                pDestination->Format("%04X CB 1E    RR (HL)", address);
                return true;

            case 0x1F:
                pDestination->Format("%04X CB 1F    RR A", address);
                return true;

            case 0x20:
                pDestination->Format("%04X CB 20    SLA B", address);
                return true;

            case 0x21:
                pDestination->Format("%04X CB 21    SLA C", address);
                return true;

            case 0x22:
                pDestination->Format("%04X CB 22    SLA D", address);
                return true;

            case 0x23:
                pDestination->Format("%04X CB 23    SLA E", address);
                return true;

            case 0x24:
                pDestination->Format("%04X CB 24    SLA H", address);
                return true;

            case 0x25:
                pDestination->Format("%04X CB 25    SLA L", address);
                return true;

            case 0x26:
                pDestination->Format("%04X CB 26    SLA (HL)", address);
                return true;

            case 0x27:
                pDestination->Format("%04X CB 27    SLA A", address);
                return true;

            case 0x28:
                pDestination->Format("%04X CB 28    SRA B", address);
                return true;

            case 0x29:
                pDestination->Format("%04X CB 29    SRA C", address);
                return true;

            case 0x2A:
                pDestination->Format("%04X CB 2A    SRA D", address);
                return true;

            case 0x2B:
                pDestination->Format("%04X CB 2B    SRA E", address);
                return true;

            case 0x2C:
                pDestination->Format("%04X CB 2C    SRA H", address);
                return true;

            case 0x2D:
                pDestination->Format("%04X CB 2D    SRA L", address);
                return true;

            case 0x2E:
                pDestination->Format("%04X CB 2E    SRA (HL)", address);
                return true;

            case 0x2F:
                pDestination->Format("%04X CB 2F    SRA A", address);
                return true;

            case 0x30:
                pDestination->Format("%04X CB 30    SWAP B", address);
                return true;

            case 0x31:
                pDestination->Format("%04X CB 31    SWAP C", address);
                return true;

            case 0x32:
                pDestination->Format("%04X CB 32    SWAP D", address);
                return true;

            case 0x33:
                pDestination->Format("%04X CB 33    SWAP E", address);
                return true;

            case 0x34:
                pDestination->Format("%04X CB 34    SWAP H", address);
                return true;

            case 0x35:
                pDestination->Format("%04X CB 35    SWAP L", address);
                return true;

            case 0x36:
                pDestination->Format("%04X CB 36    SWAP (HL)", address);
                return true;

            case 0x37:
                pDestination->Format("%04X CB 37    SWAP A", address);
                return true;

            case 0x38:
                pDestination->Format("%04X CB 38    SRL B", address);
                return true;

            case 0x39:
                pDestination->Format("%04X CB 39    SRL C", address);
                return true;

            case 0x3A:
                pDestination->Format("%04X CB 3A    SRL D", address);
                return true;

            case 0x3B:
                pDestination->Format("%04X CB 3B    SRL E", address);
                return true;

            case 0x3C:
                pDestination->Format("%04X CB 3C    SRL H", address);
                return true;

            case 0x3D:
                pDestination->Format("%04X CB 3D    SRL L", address);
                return true;

            case 0x3E:
                pDestination->Format("%04X CB 3E    SRL (HL)", address);
                return true;

            case 0x3F:
                pDestination->Format("%04X CB 3F    SRL A", address);
                return true;

            case 0x40:
                pDestination->Format("%04X CB 40    BIT 0,B", address);
                return true;

            case 0x41:
                pDestination->Format("%04X CB 41    BIT 0,C", address);
                return true;

            case 0x42:
                pDestination->Format("%04X CB 42    BIT 0,D", address);
                return true;

            case 0x43:
                pDestination->Format("%04X CB 43    BIT 0,E", address);
                return true;

            case 0x44:
                pDestination->Format("%04X CB 44    BIT 0,H", address);
                return true;

            case 0x45:
                pDestination->Format("%04X CB 45    BIT 0,L", address);
                return true;

            case 0x46:
                pDestination->Format("%04X CB 46    BIT 0,(HL)", address);
                return true;

            case 0x47:
                pDestination->Format("%04X CB 47    BIT 0,A", address);
                return true;

            case 0x48:
                pDestination->Format("%04X CB 48    BIT 1,B", address);
                return true;

            case 0x49:
                pDestination->Format("%04X CB 49    BIT 1,C", address);
                return true;

            case 0x4A:
                pDestination->Format("%04X CB 4A    BIT 1,D", address);
                return true;

            case 0x4B:
                pDestination->Format("%04X CB 4B    BIT 1,E", address);
                return true;

            case 0x4C:
                pDestination->Format("%04X CB 4C    BIT 1,H", address);
                return true;

            case 0x4D:
                pDestination->Format("%04X CB 4D    BIT 1,L", address);
                return true;

            case 0x4E:
                pDestination->Format("%04X CB 4E    BIT 1,(HL)", address);
                return true;

            case 0x4F:
                pDestination->Format("%04X CB 4F    BIT 1,A", address);
                return true;

            case 0x50:
                pDestination->Format("%04X CB 50    BIT 2,B", address);
                return true;

            case 0x51:
                pDestination->Format("%04X CB 51    BIT 2,C", address);
                return true;

            case 0x52:
                pDestination->Format("%04X CB 52    BIT 2,D", address);
                return true;

            case 0x53:
                pDestination->Format("%04X CB 53    BIT 2,E", address);
                return true;

            case 0x54:
                pDestination->Format("%04X CB 54    BIT 2,H", address);
                return true;

            case 0x55:
                pDestination->Format("%04X CB 55    BIT 2,L", address);
                return true;

            case 0x56:
                pDestination->Format("%04X CB 56    BIT 2,(HL)", address);
                return true;

            case 0x57:
                pDestination->Format("%04X CB 57    BIT 2,A", address);
                return true;

            case 0x58:
                pDestination->Format("%04X CB 58    BIT 3,B", address);
                return true;

            case 0x59:
                pDestination->Format("%04X CB 59    BIT 3,C", address);
                return true;

            case 0x5A:
                pDestination->Format("%04X CB 5A    BIT 3,D", address);
                return true;

            case 0x5B:
                pDestination->Format("%04X CB 5B    BIT 3,E", address);
                return true;

            case 0x5C:
                pDestination->Format("%04X CB 5C    BIT 3,H", address);
                return true;

            case 0x5D:
                pDestination->Format("%04X CB 5D    BIT 3,L", address);
                return true;

            case 0x5E:
                pDestination->Format("%04X CB 5E    BIT 3,(HL)", address);
                return true;

            case 0x5F:
                pDestination->Format("%04X CB 5F    BIT 3,A", address);
                return true;

            case 0x60:
                pDestination->Format("%04X CB 60    BIT 4,B", address);
                return true;

            case 0x61:
                pDestination->Format("%04X CB 61    BIT 4,C", address);
                return true;

            case 0x62:
                pDestination->Format("%04X CB 62    BIT 4,D", address);
                return true;

            case 0x63:
                pDestination->Format("%04X CB 63    BIT 4,E", address);
                return true;

            case 0x64:
                pDestination->Format("%04X CB 64    BIT 4,H", address);
                return true;

            case 0x65:
                pDestination->Format("%04X CB 65    BIT 4,L", address);
                return true;

            case 0x66:
                pDestination->Format("%04X CB 66    BIT 4,(HL)", address);
                return true;

            case 0x67:
                pDestination->Format("%04X CB 67    BIT 4,A", address);
                return true;

            case 0x68:
                pDestination->Format("%04X CB 68    BIT 5,B", address);
                return true;

            case 0x69:
                pDestination->Format("%04X CB 69    BIT 5,C", address);
                return true;

            case 0x6A:
                pDestination->Format("%04X CB 6A    BIT 5,D", address);
                return true;

            case 0x6B:
                pDestination->Format("%04X CB 6B    BIT 5,E", address);
                return true;

            case 0x6C:
                pDestination->Format("%04X CB 6C    BIT 5,H", address);
                return true;

            case 0x6D:
                pDestination->Format("%04X CB 6D    BIT 5,L", address);
                return true;

            case 0x6E:
                pDestination->Format("%04X CB 6E    BIT 5,(HL)", address);
                return true;

            case 0x6F:
                pDestination->Format("%04X CB 6F    BIT 5,A", address);
                return true;

            case 0x70:
                pDestination->Format("%04X CB 70    BIT 6,B", address);
                return true;

            case 0x71:
                pDestination->Format("%04X CB 71    BIT 6,C", address);
                return true;

            case 0x72:
                pDestination->Format("%04X CB 72    BIT 6,D", address);
                return true;

            case 0x73:
                pDestination->Format("%04X CB 73    BIT 6,E", address);
                return true;

            case 0x74:
                pDestination->Format("%04X CB 74    BIT 6,H", address);
                return true;

            case 0x75:
                pDestination->Format("%04X CB 75    BIT 6,L", address);
                return true;

            case 0x76:
                pDestination->Format("%04X CB 76    BIT 6,(HL)", address);
                return true;

            case 0x77:
                pDestination->Format("%04X CB 77    BIT 6,A", address);
                return true;

            case 0x78:
                pDestination->Format("%04X CB 78    BIT 7,B", address);
                return true;

            case 0x79:
                pDestination->Format("%04X CB 79    BIT 7,C", address);
                return true;

            case 0x7A:
                pDestination->Format("%04X CB 7A    BIT 7,D", address);
                return true;

            case 0x7B:
                pDestination->Format("%04X CB 7B    BIT 7,E", address);
                return true;

            case 0x7C:
                pDestination->Format("%04X CB 7C    BIT 7,H", address);
                return true;

            case 0x7D:
                pDestination->Format("%04X CB 7D    BIT 7,L", address);
                return true;

            case 0x7E:
                pDestination->Format("%04X CB 7E    BIT 7,(HL)", address);
                return true;

            case 0x7F:
                pDestination->Format("%04X CB 7F    BIT 7,A", address);
                return true;

            case 0x80:
                pDestination->Format("%04X CB 80    RES 0,B", address);
                return true;

            case 0x81:
                pDestination->Format("%04X CB 81    RES 0,C", address);
                return true;

            case 0x82:
                pDestination->Format("%04X CB 82    RES 0,D", address);
                return true;

            case 0x83:
                pDestination->Format("%04X CB 83    RES 0,E", address);
                return true;

            case 0x84:
                pDestination->Format("%04X CB 84    RES 0,H", address);
                return true;

            case 0x85:
                pDestination->Format("%04X CB 85    RES 0,L", address);
                return true;

            case 0x86:
                pDestination->Format("%04X CB 86    RES 0,(HL)", address);
                return true;

            case 0x87:
                pDestination->Format("%04X CB 87    RES 0,A", address);
                return true;

            case 0x88:
                pDestination->Format("%04X CB 88    RES 1,B", address);
                return true;

            case 0x89:
                pDestination->Format("%04X CB 89    RES 1,C", address);
                return true;

            case 0x8A:
                pDestination->Format("%04X CB 8A    RES 1,D", address);
                return true;

            case 0x8B:
                pDestination->Format("%04X CB 8B    RES 1,E", address);
                return true;

            case 0x8C:
                pDestination->Format("%04X CB 8C    RES 1,H", address);
                return true;

            case 0x8D:
                pDestination->Format("%04X CB 8D    RES 1,L", address);
                return true;

            case 0x8E:
                pDestination->Format("%04X CB 8E    RES 1,(HL)", address);
                return true;

            case 0x8F:
                pDestination->Format("%04X CB 8F    RES 1,A", address);
                return true;

            case 0x90:
                pDestination->Format("%04X CB 90    RES 2,B", address);
                return true;

            case 0x91:
                pDestination->Format("%04X CB 91    RES 2,C", address);
                return true;

            case 0x92:
                pDestination->Format("%04X CB 92    RES 2,D", address);
                return true;

            case 0x93:
                pDestination->Format("%04X CB 93    RES 2,E", address);
                return true;

            case 0x94:
                pDestination->Format("%04X CB 94    RES 2,H", address);
                return true;

            case 0x95:
                pDestination->Format("%04X CB 95    RES 2,L", address);
                return true;

            case 0x96:
                pDestination->Format("%04X CB 96    RES 2,(HL)", address);
                return true;

            case 0x97:
                pDestination->Format("%04X CB 97    RES 2,A", address);
                return true;

            case 0x98:
                pDestination->Format("%04X CB 98    RES 3,B", address);
                return true;

            case 0x99:
                pDestination->Format("%04X CB 99    RES 3,C", address);
                return true;

            case 0x9A:
                pDestination->Format("%04X CB 9A    RES 3,D", address);
                return true;

            case 0x9B:
                pDestination->Format("%04X CB 9B    RES 3,E", address);
                return true;

            case 0x9C:
                pDestination->Format("%04X CB 9C    RES 3,H", address);
                return true;

            case 0x9D:
                pDestination->Format("%04X CB 9D    RES 3,L", address);
                return true;

            case 0x9E:
                pDestination->Format("%04X CB 9E    RES 3,(HL)", address);
                return true;

            case 0x9F:
                pDestination->Format("%04X CB 9F    RES 3,A", address);
                return true;

            case 0xA0:
                pDestination->Format("%04X CB A0    RES 4,B", address);
                return true;

            case 0xA1:
                pDestination->Format("%04X CB A1    RES 4,C", address);
                return true;

            case 0xA2:
                pDestination->Format("%04X CB A2    RES 4,D", address);
                return true;

            case 0xA3:
                pDestination->Format("%04X CB A3    RES 4,E", address);
                return true;

            case 0xA4:
                pDestination->Format("%04X CB A4    RES 4,H", address);
                return true;

            case 0xA5:
                pDestination->Format("%04X CB A5    RES 4,L", address);
                return true;

            case 0xA6:
                pDestination->Format("%04X CB A6    RES 4,(HL)", address);
                return true;

            case 0xA7:
                pDestination->Format("%04X CB A7    RES 4,A", address);
                return true;

            case 0xA8:
                pDestination->Format("%04X CB A8    RES 5,B", address);
                return true;

            case 0xA9:
                pDestination->Format("%04X CB A9    RES 5,C", address);
                return true;

            case 0xAA:
                pDestination->Format("%04X CB AA    RES 5,D", address);
                return true;

            case 0xAB:
                pDestination->Format("%04X CB AB    RES 5,E", address);
                return true;

            case 0xAC:
                pDestination->Format("%04X CB AC    RES 5,H", address);
                return true;

            case 0xAD:
                pDestination->Format("%04X CB AD    RES 5,L", address);
                return true;

            case 0xAE:
                pDestination->Format("%04X CB AE    RES 5,(HL)", address);
                return true;

            case 0xAF:
                pDestination->Format("%04X CB AF    RES 5,A", address);
                return true;

            case 0xB0:
                pDestination->Format("%04X CB B0    RES 6,B", address);
                return true;

            case 0xB1:
                pDestination->Format("%04X CB B1    RES 6,C", address);
                return true;

            case 0xB2:
                pDestination->Format("%04X CB B2    RES 6,D", address);
                return true;

            case 0xB3:
                pDestination->Format("%04X CB B3    RES 6,E", address);
                return true;

            case 0xB4:
                pDestination->Format("%04X CB B4    RES 6,H", address);
                return true;

            case 0xB5:
                pDestination->Format("%04X CB B5    RES 6,L", address);
                return true;

            case 0xB6:
                pDestination->Format("%04X CB B6    RES 6,(HL)", address);
                return true;

            case 0xB7:
                pDestination->Format("%04X CB B7    RES 6,A", address);
                return true;

            case 0xB8:
                pDestination->Format("%04X CB B8    RES 7,B", address);
                return true;

            case 0xB9:
                pDestination->Format("%04X CB B9    RES 7,C", address);
                return true;

            case 0xBA:
                pDestination->Format("%04X CB BA    RES 7,D", address);
                return true;

            case 0xBB:
                pDestination->Format("%04X CB BB    RES 7,E", address);
                return true;

            case 0xBC:
                pDestination->Format("%04X CB BC    RES 7,H", address);
                return true;

            case 0xBD:
                pDestination->Format("%04X CB BD    RES 7,L", address);
                return true;

            case 0xBE:
                pDestination->Format("%04X CB BE    RES 7,(HL)", address);
                return true;

            case 0xBF:
                pDestination->Format("%04X CB BF    RES 7,A", address);
                return true;

            case 0xC0:
                pDestination->Format("%04X CB C0    SET 0,B", address);
                return true;

            case 0xC1:
                pDestination->Format("%04X CB C1    SET 0,C", address);
                return true;

            case 0xC2:
                pDestination->Format("%04X CB C2    SET 0,D", address);
                return true;

            case 0xC3:
                pDestination->Format("%04X CB C3    SET 0,E", address);
                return true;

            case 0xC4:
                pDestination->Format("%04X CB C4    SET 0,H", address);
                return true;

            case 0xC5:
                pDestination->Format("%04X CB C5    SET 0,L", address);
                return true;

            case 0xC6:
                pDestination->Format("%04X CB C6    SET 0,(HL)", address);
                return true;

            case 0xC7:
                pDestination->Format("%04X CB C7    SET 0,A", address);
                return true;

            case 0xC8:
                pDestination->Format("%04X CB C8    SET 1,B", address);
                return true;

            case 0xC9:
                pDestination->Format("%04X CB C9    SET 1,C", address);
                return true;

            case 0xCA:
                pDestination->Format("%04X CB CA    SET 1,D", address);
                return true;

            case 0xCB:
                pDestination->Format("%04X CB CB    SET 1,E", address);
                return true;

            case 0xCC:
                pDestination->Format("%04X CB CC    SET 1,H", address);
                return true;

            case 0xCD:
                pDestination->Format("%04X CB CD    SET 1,L", address);
                return true;

            case 0xCE:
                pDestination->Format("%04X CB CE    SET 1,(HL)", address);
                return true;

            case 0xCF:
                pDestination->Format("%04X CB CF    SET 1,A", address);
                return true;

            case 0xD0:
                pDestination->Format("%04X CB D0    SET 2,B", address);
                return true;

            case 0xD1:
                pDestination->Format("%04X CB D1    SET 2,C", address);
                return true;

            case 0xD2:
                pDestination->Format("%04X CB D2    SET 2,D", address);
                return true;

            case 0xD3:
                pDestination->Format("%04X CB D3    SET 2,E", address);
                return true;

            case 0xD4:
                pDestination->Format("%04X CB D4    SET 2,H", address);
                return true;

            case 0xD5:
                pDestination->Format("%04X CB D5    SET 2,L", address);
                return true;

            case 0xD6:
                pDestination->Format("%04X CB D6    SET 2,(HL)", address);
                return true;

            case 0xD7:
                pDestination->Format("%04X CB D7    SET 2,A", address);
                return true;

            case 0xD8:
                pDestination->Format("%04X CB D8    SET 3,B", address);
                return true;

            case 0xD9:
                pDestination->Format("%04X CB D9    SET 3,C", address);
                return true;

            case 0xDA:
                pDestination->Format("%04X CB DA    SET 3,D", address);
                return true;

            case 0xDB:
                pDestination->Format("%04X CB DB    SET 3,E", address);
                return true;

            case 0xDC:
                pDestination->Format("%04X CB DC    SET 3,H", address);
                return true;

            case 0xDD:
                pDestination->Format("%04X CB DD    SET 3,L", address);
                return true;

            case 0xDE:
                pDestination->Format("%04X CB DE    SET 3,(HL)", address);
                return true;

            case 0xDF:
                pDestination->Format("%04X CB DF    SET 3,A", address);
                return true;

            case 0xE0:
                pDestination->Format("%04X CB E0    SET 4,B", address);
                return true;

            case 0xE1:
                pDestination->Format("%04X CB E1    SET 4,C", address);
                return true;

            case 0xE2:
                pDestination->Format("%04X CB E2    SET 4,D", address);
                return true;

            case 0xE3:
                pDestination->Format("%04X CB E3    SET 4,E", address);
                return true;

            case 0xE4:
                pDestination->Format("%04X CB E4    SET 4,H", address);
                return true;

            case 0xE5:
                pDestination->Format("%04X CB E5    SET 4,L", address);
                return true;

            case 0xE6:
                pDestination->Format("%04X CB E6    SET 4,(HL)", address);
                return true;

            case 0xE7:
                pDestination->Format("%04X CB E7    SET 4,A", address);
                return true;

            case 0xE8:
                pDestination->Format("%04X CB E8    SET 5,B", address);
                return true;

            case 0xE9:
                pDestination->Format("%04X CB E9    SET 5,C", address);
                return true;

            case 0xEA:
                pDestination->Format("%04X CB EA    SET 5,D", address);
                return true;

            case 0xEB:
                pDestination->Format("%04X CB EB    SET 5,E", address);
                return true;

            case 0xEC:
                pDestination->Format("%04X CB EC    SET 5,H", address);
                return true;

            case 0xED:
                pDestination->Format("%04X CB ED    SET 5,L", address);
                return true;

            case 0xEE:
                pDestination->Format("%04X CB EE    SET 5,(HL)", address);
                return true;

            case 0xEF:
                pDestination->Format("%04X CB EF    SET 5,A", address);
                return true;

            case 0xF0:
                pDestination->Format("%04X CB F0    SET 6,B", address);
                return true;

            case 0xF1:
                pDestination->Format("%04X CB F1    SET 6,C", address);
                return true;

            case 0xF2:
                pDestination->Format("%04X CB F2    SET 6,D", address);
                return true;

            case 0xF3:
                pDestination->Format("%04X CB F3    SET 6,E", address);
                return true;

            case 0xF4:
                pDestination->Format("%04X CB F4    SET 6,H", address);
                return true;

            case 0xF5:
                pDestination->Format("%04X CB F5    SET 6,L", address);
                return true;

            case 0xF6:
                pDestination->Format("%04X CB F6    SET 6,(HL)", address);
                return true;

            case 0xF7:
                pDestination->Format("%04X CB F7    SET 6,A", address);
                return true;

            case 0xF8:
                pDestination->Format("%04X CB F8    SET 7,B", address);
                return true;

            case 0xF9:
                pDestination->Format("%04X CB F9    SET 7,C", address);
                return true;

            case 0xFA:
                pDestination->Format("%04X CB FA    SET 7,D", address);
                return true;

            case 0xFB:
                pDestination->Format("%04X CB FB    SET 7,E", address);
                return true;

            case 0xFC:
                pDestination->Format("%04X CB FC    SET 7,H", address);
                return true;

            case 0xFD:
                pDestination->Format("%04X CB FD    SET 7,L", address);
                return true;

            case 0xFE:
                pDestination->Format("%04X CB FE    SET 7,(HL)", address);
                return true;

            case 0xFF:
                pDestination->Format("%04X CB FF    SET 7,A", address);
                return true;
            }
        }
        break;

    case 0xCC:
        pDestination->Format("%04X CC       CALL Z,nn", address);
        return true;

    case 0xCD:
        pDestination->Format("%04X CD %02X %02X CALL $%04X", address, imm16b1, imm16b2, imm16);
        return true;

    case 0xCE:
        pDestination->Format("%04X CE       ADC A,n", address);
        return true;

    case 0xCF:
        pDestination->Format("%04X CF       RST 8", address);
        return true;

    case 0xD0:
        pDestination->Format("%04X D0       RET NC", address);
        return true;

    case 0xD1:
        pDestination->Format("%04X D1       POP DE", address);
        return true;

    case 0xD2:
        pDestination->Format("%04X D2       JP NC,nn", address);
        return true;

    case 0xD3:
        pDestination->Format("%04X D3       XX", address);
        return true;

    case 0xD4:
        pDestination->Format("%04X D4       CALL NC,nn", address);
        return true;

    case 0xD5:
        pDestination->Format("%04X D5       PUSH DE", address);
        return true;

    case 0xD6:
        pDestination->Format("%04X D6       SUB A,n", address);
        return true;

    case 0xD7:
        pDestination->Format("%04X D7       RST 10", address);
        return true;

    case 0xD8:
        pDestination->Format("%04X D8       RET C", address);
        return true;

    case 0xD9:
        pDestination->Format("%04X D9       RETI", address);
        return true;

    case 0xDA:
        pDestination->Format("%04X DA       JP C,nn", address);
        return true;

    case 0xDB:
        pDestination->Format("%04X DB       XX", address);
        return true;

    case 0xDC:
        pDestination->Format("%04X DC       CALL C,nn", address);
        return true;

    case 0xDD:
        pDestination->Format("%04X DD       XX", address);
        return true;

    case 0xDE:
        pDestination->Format("%04X DE       SBC A,n", address);
        return true;

    case 0xDF:
        pDestination->Format("%04X DF       RST 18", address);
        return true;

    case 0xE0:
        pDestination->Format("%04X E0 %02X    LDH $%02X,A", address, imm8, imm8);
        return true;

    case 0xE1:
        pDestination->Format("%04X E1       POP HL", address);
        return true;

    case 0xE2:
        pDestination->Format("%04X E2       LDH (C),A", address);
        return true;

    case 0xE3:
        pDestination->Format("%04X E3       XX", address);
        return true;

    case 0xE4:
        pDestination->Format("%04X E4       XX", address);
        return true;

    case 0xE5:
        pDestination->Format("%04X E5       PUSH HL", address);
        return true;

    case 0xE6:
        pDestination->Format("%04X E6 %02X    AND $%02X", address, imm8, imm8);
        return true;

    case 0xE7:
        pDestination->Format("%04X E7       RST 20", address);
        return true;

    case 0xE8:
        pDestination->Format("%04X E8       ADD SP,d", address);
        return true;

    case 0xE9:
        pDestination->Format("%04X E9       JP (HL)", address);
        return true;

    case 0xEA:
        pDestination->Format("%04X EA       LD $%04X,A", imm16, address);
        return true;

    case 0xEB:
        pDestination->Format("%04X EB       XX", address);
        return true;

    case 0xEC:
        pDestination->Format("%04X EC       XX", address);
        return true;

    case 0xED:
        pDestination->Format("%04X ED       XX", address);
        return true;

    case 0xEE:
        pDestination->Format("%04X EE       XOR n", address);
        return true;

    case 0xEF:
        pDestination->Format("%04X EF       RST 28", address);
        return true;

    case 0xF0:
        pDestination->Format("%04X F0 %02X    LDH A,($FF00+$%02X)", address, imm8, imm8);
        return true;

    case 0xF1:
        pDestination->Format("%04X F1       POP AF", address);
        return true;

    case 0xF2:
        pDestination->Format("%04X F2       XX", address);
        return true;

    case 0xF3:
        pDestination->Format("%04X F3       DI", address);
        return true;

    case 0xF4:
        pDestination->Format("%04X F4       XX", address);
        return true;

    case 0xF5:
        pDestination->Format("%04X F5       PUSH AF", address);
        return true;

    case 0xF6:
        pDestination->Format("%04X F6       OR n", address);
        return true;

    case 0xF7:
        pDestination->Format("%04X F7       RST 30", address);
        return true;

    case 0xF8:
        pDestination->Format("%04X F8       LDHL SP,d", address);
        return true;

    case 0xF9:
        pDestination->Format("%04X F9       LD SP,HL", address);
        return true;

    case 0xFA:
        pDestination->Format("%04X FA       LD A,($%04X)", address, imm16);
        return true;

    case 0xFB:
        pDestination->Format("%04X FB       EI", address);
        return true;

    case 0xFC:
        pDestination->Format("%04X FC       XX", address);
        return true;

    case 0xFD:
        pDestination->Format("%04X FD       XX", address);
        return true;

    case 0xFE:
        pDestination->Format("%04X FE %02X    CP %u", address, imm8, imm8);
        return true;

    case 0xFF:
        pDestination->Format("%04X FF       RST 38", address);
        return true;
    }

    return false;
}
