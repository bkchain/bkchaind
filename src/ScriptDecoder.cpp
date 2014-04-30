#include "ScriptDecoder.h"

#include <sstream>
#include <iomanip>
#include <algorithm>

void decodeScript(const uint8_t *script, uint32_t scriptLength, std::ostringstream &decodedScript)
{
	bool ret = true;

	const uint8_t *endOfScript = script + scriptLength;
	while (script < endOfScript && ret)
	{
		uint8_t opcode = *script++;
		if (opcode < OP_PUSHDATA1)	// Any opcode less than OP_PUSHDATA1 is simply an instruction to push that many bytes onto the stack
		{
			decodedScript << std::hex << std::uppercase << std::setfill('0');
			std::for_each(script, script + opcode, [&](int c) { decodedScript << std::setw(2) << c; });
			script += opcode;
		}
		else
		{
			switch (opcode)
			{
			case OP_0:          decodedScript << "OP_0"; break;
			case OP_PUSHDATA1:  decodedScript << "OP_PUSHDATA1"; break;
			case OP_PUSHDATA2:  decodedScript << "OP_PUSHDATA2"; break;
			case OP_PUSHDATA4:  decodedScript << "OP_PUSHDATA4"; break;
			case OP_1NEGATE:    decodedScript << "OP_1NEGATE"; break;
			case OP_RESERVED:   decodedScript << "OP_RESERVED"; break;
			case OP_1:          decodedScript << "OP_1"; break;
			case OP_2:          decodedScript << "OP_2"; break;
			case OP_3:          decodedScript << "OP_3"; break;
			case OP_4:          decodedScript << "OP_4"; break;
			case OP_5:          decodedScript << "OP_5"; break;
			case OP_6:          decodedScript << "OP_6"; break;
			case OP_7:          decodedScript << "OP_7"; break;
			case OP_8:          decodedScript << "OP_8"; break;
			case OP_9:          decodedScript << "OP_9"; break;
			case OP_10:         decodedScript << "OP_10"; break;
			case OP_11:         decodedScript << "OP_11"; break;
			case OP_12:         decodedScript << "OP_12"; break;
			case OP_13:         decodedScript << "OP_13"; break;
			case OP_14:         decodedScript << "OP_14"; break;
			case OP_15:         decodedScript << "OP_15"; break;
			case OP_16:         decodedScript << "OP_16"; break;
			case OP_NOP:        decodedScript << "OP_NOP"; break;
			case OP_VER:        decodedScript << "OP_VER"; break;
			case OP_IF:         decodedScript << "OP_IF"; break;
			case OP_NOTIF:      decodedScript << "OP_NOTIF"; break;
			case OP_VERIF:      decodedScript << "OP_VERIF"; break;
			case OP_VERNOTIF:   decodedScript << "OP_VERNOTIF"; break;
			case OP_ELSE:       decodedScript << "OP_ELSE"; break;
			case OP_ENDIF:      decodedScript << "OP_ENDIF"; break;
			case OP_VERIFY:     decodedScript << "OP_VERIFY"; break;
			case OP_RETURN:     decodedScript << "OP_RETURN"; break;
			case OP_TOALTSTACK: decodedScript << "OP_TOALTSTACK"; break;
			case OP_FROMALTSTACK:  decodedScript << "OP_FROMALTSTACK"; break;
			case OP_2DROP:      decodedScript << "OP_2DROP"; break;
			case OP_2DUP:       decodedScript << "OP_2DUP"; break;
			case OP_3DUP:       decodedScript << "OP_3DUP"; break;
			case OP_2OVER:      decodedScript << "OP_2OVER"; break;
			case OP_2ROT:       decodedScript << "OP_2ROT"; break;
			case OP_2SWAP:      decodedScript << "OP_2SWAP"; break;
			case OP_IFDUP:      decodedScript << "OP_IFDUP"; break;
			case OP_DEPTH:      decodedScript << "OP_DEPTH"; break;
			case OP_DROP:       decodedScript << "OP_DROP"; break;
			case OP_DUP:        decodedScript << "OP_DUP"; break;
			case OP_NIP:        decodedScript << "OP_NIP"; break;
			case OP_OVER:       decodedScript << "OP_OVER"; break;
			case OP_PICK:       decodedScript << "OP_PICK"; break;
			case OP_ROLL:       decodedScript << "OP_ROLL"; break;
			case OP_ROT:   decodedScript << "OP_ROT"; break;
			case OP_SWAP:  decodedScript << "OP_SWAP"; break;
			case OP_TUCK:  decodedScript << "OP_TUCK"; break;
			case OP_CAT:   decodedScript << "OP_CAT"; break;
			case OP_SUBSTR:  decodedScript << "OP_SUBSTR"; break;
			case OP_LEFT:   decodedScript << "OP_LEFT"; break;
			case OP_RIGHT:   decodedScript << "OP_RIGHT"; break;
			case OP_SIZE:   decodedScript << "OP_SIZE"; break;
			case OP_INVERT:  decodedScript << "OP_INVERT"; break;
			case OP_AND:   decodedScript << "OP_AND"; break;
			case OP_OR:   decodedScript << "OP_OR"; break;
			case OP_XOR:   decodedScript << "OP_XOR"; break;
			case OP_EQUAL:   decodedScript << "OP_EQUAL"; break;
			case OP_EQUALVERIFY:  decodedScript << "OP_EQUALVERIFY"; break;
			case OP_RESERVED1:  decodedScript << "OP_RESERVED1"; break;
			case OP_RESERVED2:  decodedScript << "OP_RESERVED2"; break;
			case OP_1ADD:  decodedScript << "OP_1ADD"; break;
			case OP_1SUB:  decodedScript << "OP_1SUB"; break;
			case OP_2MUL:  decodedScript << "OP_2MUL"; break;
			case OP_2DIV:  decodedScript << "OP_2DIV"; break;
			case OP_NEGATE:  decodedScript << "OP_NEGATE"; break;
			case OP_ABS:  decodedScript << "OP_ABS"; break;
			case OP_NOT:  decodedScript << "OP_NOT"; break;
			case OP_0NOTEQUAL:  decodedScript << "OP_0NOTEQUAL"; break;
			case OP_ADD:  decodedScript << "OP_ADD"; break;
			case OP_SUB:  decodedScript << "OP_SUB"; break;
			case OP_MUL:  decodedScript << "OP_MUL"; break;
			case OP_DIV:  decodedScript << "OP_DIV"; break;
			case OP_MOD:  decodedScript << "OP_MOD"; break;
			case OP_LSHIFT:  decodedScript << "OP_LSHIFT"; break;
			case OP_RSHIFT:  decodedScript << "OP_RSHIFT"; break;
			case OP_BOOLAND:  decodedScript << "OP_BOOLAND"; break;
			case OP_BOOLOR:  decodedScript << "OP_BOOLOR"; break;
			case OP_NUMEQUAL:  decodedScript << "OP_NUMEQUAL"; break;
			case OP_NUMEQUALVERIFY:  decodedScript << "OP_NUMEQUALVERIFY"; break;
			case OP_NUMNOTEQUAL:  decodedScript << "OP_NUMNOTEQUAL"; break;
			case OP_LESSTHAN:  decodedScript << "OP_LESSTHAN"; break;
			case OP_GREATERTHAN:  decodedScript << "OP_GREATERTHAN"; break;
			case OP_LESSTHANOREQUAL:  decodedScript << "OP_LESSTHANOREQUAL"; break;
			case OP_GREATERTHANOREQUAL:  decodedScript << "OP_GREATERTHANOREQUAL"; break;
			case OP_MIN:  decodedScript << "OP_MIN"; break;
			case OP_MAX:  decodedScript << "OP_MAX"; break;
			case OP_WITHIN:  decodedScript << "OP_WITHIN"; break;
			case OP_RIPEMD160:  decodedScript << "OP_RIPEMD160"; break;
			case OP_SHA1:  decodedScript << "OP_SHA1"; break;
			case OP_SHA256:  decodedScript << "OP_SHA256"; break;
			case OP_HASH160:  decodedScript << "OP_HASH160"; break;
			case OP_HASH256:  decodedScript << "OP_HASH256"; break;
			case OP_CODESEPARATOR:  decodedScript << "OP_CODESEPARATOR"; break;
			case OP_CHECKSIG:   decodedScript << "OP_CHECKSIG"; break;
			case OP_CHECKSIGVERIFY:   decodedScript << "OP_CHECKSIGVERIFY"; break;
			case OP_CHECKMULTISIG:   decodedScript << "OP_CHECKMULTISIG"; break;
			case OP_CHECKMULTISIGVERIFY:  decodedScript << "OP_CHECKMULTISIGVERIFY"; break;
			case OP_NOP1:   decodedScript << "OP_NOP1"; break;
			case OP_NOP2:   decodedScript << "OP_NOP2"; break;
			case OP_NOP3:   decodedScript << "OP_NOP3"; break;
			case OP_NOP4:   decodedScript << "OP_NOP4"; break;
			case OP_NOP5:   decodedScript << "OP_NOP5"; break;
			case OP_NOP6:   decodedScript << "OP_NOP6"; break;
			case OP_NOP7:   decodedScript << "OP_NOP7"; break;
			case OP_NOP8:   decodedScript << "OP_NOP8"; break;
			case OP_NOP9:   decodedScript << "OP_NOP9"; break;
			case OP_NOP10:   decodedScript << "OP_NOP10"; break;
			case OP_SMALLINTEGER:   decodedScript << "OP_SMALLINTEGER"; break;
			case OP_PUBKEYS:   decodedScript << "OP_PUBKEYS"; break;
			case OP_PUBKEYHASH:   decodedScript << "OP_PUBKEYHASH"; break;
			case OP_PUBKEY:   decodedScript << "OP_PUBKEY"; break;
			case OP_INVALIDOPCODE:   decodedScript << "OP_INVALIDOPCODE"; break;
			}
		}

		if (script != endOfScript)
			decodedScript << " ";
	}
}
