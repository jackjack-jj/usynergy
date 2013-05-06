/*
uSynergy client -- Implementation for the embedded Synergy client library
  version 1.0.0, July 7th, 2012

Copyright (c) 2012 Alex Evans

This software is provided 'as-is', without any express or implied
warranty. In no event will the authors be held liable for any damages
arising from the use of this software.

Permission is granted to anyone to use this software for any purpose,
including commercial applications, and to alter it and redistribute it
freely, subject to the following restrictions:

   1. The origin of this software must not be misrepresented; you must not
   claim that you wrote the original software. If you use this software
   in a product, an acknowledgment in the product documentation would be
   appreciated but is not required.

   2. Altered source versions must be plainly marked as such, and must not be
   misrepresented as being the original software.

   3. This notice may not be removed or altered from any source
   distribution.
*/
#include "uSynergy.h"
#include "suinput.h"
#include <stdio.h>
#include <string.h>

#define msleep(n) usleep(n*1000)
//---------------------------------------------------------------------------------------------------------------------
//	Internal helpers
//---------------------------------------------------------------------------------------------------------------------



/**
@brief Read 16 bit integer in network byte order and convert to native byte order
**/
static int16_t sNetToNative16(const unsigned char *value)
{
#ifdef USYNERGY_LITTLE_ENDIAN
	return value[1] | (value[0] << 8);
#else
	return value[0] | (value[1] << 8);
#endif
}



/**
@brief Read 32 bit integer in network byte order and convert to native byte order
**/
static int32_t sNetToNative32(const unsigned char *value)
{
#ifdef USYNERGY_LITTLE_ENDIAN
	return value[3] | (value[2] << 8) | (value[1] << 16) | (value[0] << 24);
#else
	return value[0] | (value[1] << 8) | (value[2] << 16) | (value[3] << 24);
#endif
}



/**
@brief Trace text to client
**/
static void sTrace(uSynergyContext *context, const char* text)
{
	// Don't trace if we don't have a trace function
	if (context->m_traceFunc != 0L)
		context->m_traceFunc(context->m_cookie, text);
}



/**
@brief Add string to reply packet
**/
static void sAddString(uSynergyContext *context, const char *string)
{
	size_t len = strlen(string);
	memcpy(context->m_replyCur, string, len);
	context->m_replyCur += len;
}



/**
@brief Add uint8 to reply packet
**/
static void sAddUInt8(uSynergyContext *context, uint8_t value)
{
	*context->m_replyCur++ = value;
}



/**
@brief Add uint16 to reply packet
**/
static void sAddUInt16(uSynergyContext *context, uint16_t value)
{
	uint8_t *reply = context->m_replyCur;
	*reply++ = (uint8_t)(value >> 8);
	*reply++ = (uint8_t)value;
	context->m_replyCur = reply;
}



/**
@brief Add uint32 to reply packet
**/
static void sAddUInt32(uSynergyContext *context, uint32_t value)
{
	uint8_t *reply = context->m_replyCur;
	*reply++ = (uint8_t)(value >> 24);
	*reply++ = (uint8_t)(value >> 16);
	*reply++ = (uint8_t)(value >> 8);
	*reply++ = (uint8_t)value;
	context->m_replyCur = reply;
}



/**
@brief Send reply packet
**/
static uSynergyBool sSendReply(uSynergyContext *context)
{
	// Set header size
	uint8_t		*reply_buf	= context->m_replyBuffer;
	uint32_t	reply_len	= (uint32_t)(context->m_replyCur - reply_buf);				/* Total size of reply */
	uint32_t	body_len	= reply_len - 4;											/* Size of body */
	uSynergyBool ret;
	reply_buf[0] = (uint8_t)(body_len >> 24);
	reply_buf[1] = (uint8_t)(body_len >> 16);
	reply_buf[2] = (uint8_t)(body_len >> 8);
	reply_buf[3] = (uint8_t)body_len;

	// Send reply
	ret = context->m_sendFunc(context->m_cookie, context->m_replyBuffer, reply_len);

	// Reset reply buffer write pointer
	context->m_replyCur = context->m_replyBuffer+4;
	return ret;
}



/**
@brief Call mouse callback after a mouse event
**/
static void sSendMouseMoveCallback(uSynergyContext *context)
{
	// Skip if no callback is installed
	if (context->m_mouseMoveCallback == 0L)
		return;

	// Send callback
	uSynergyBool ret;
	int32_t rel_x = context->m_mouseX - context->m_mouseX_old;
	int32_t rel_y = context->m_mouseY - context->m_mouseY_old;
	ret = context->m_mouseMoveCallback(context->m_cookie, rel_x, rel_y);

	if (ret == USYNERGY_TRUE) {
//printf("%d:%d -> %d:%d\n", context->m_mouseX_old, context->m_mouseY_old, context->m_mouseX, context->m_mouseY);
		context->m_mouseX_old = context->m_mouseX;
		context->m_mouseY_old = context->m_mouseY;
	}
}

static void sSendMouseUpCallback(uSynergyContext *context)
{
	if (context->m_mouseUpCallback == 0L)
		return;
	uSynergyBool ret;
	ret = context->m_mouseUpCallback(context->m_cookie, context->m_mouseButtonLeft, context->m_mouseButtonRight, context->m_mouseButtonMiddle);
}

static void sSendMouseDownCallback(uSynergyContext *context)
{
	if (context->m_mouseDownCallback == 0L)
		return;
	uSynergyBool ret;
	ret = context->m_mouseDownCallback(context->m_cookie, context->m_mouseButtonLeft, context->m_mouseButtonRight, context->m_mouseButtonMiddle);
}

static void sSendMouseWheelCallback(uSynergyContext *context)
{
	if (context->m_mouseWheelCallback == 0L)
		return;
	uSynergyBool ret;
	ret = context->m_mouseWheelCallback(context->m_cookie, context->m_mouseWheelX, context->m_mouseWheelY);
}

/**
@brief Send keyboard callback when a key has been pressed or released
**/
static void sSendKeyboardCallback(uSynergyContext *context, uint16_t key, uint16_t modifiers, uSynergyBool down, uSynergyBool repeat)
{
	// Skip if no callback is installed
	if (context->m_keyboardCallback == 0L)
		return;

	// Send callback
	context->m_keyboardCallback(context->m_cookie, key, modifiers, down, repeat);
}



/**
@brief Send joystick callback
**/
static void sSendJoystickCallback(uSynergyContext *context, uint8_t joyNum)
{
	int8_t *sticks;

	// Skip if no callback is installed
	if (context->m_joystickCallback == 0L)
		return;

	// Send callback
	sticks = context->m_joystickSticks[joyNum];
	context->m_joystickCallback(context->m_cookie, joyNum, context->m_joystickButtons[joyNum], sticks[0], sticks[1], sticks[2], sticks[3]);
}



/**
@brief Parse a single client message, update state, send callbacks and send replies
**/
#define USYNERGY_IS_PACKET(pkt_id)	memcmp(message+4, pkt_id, 4)==0
static void sProcessMessage(uSynergyContext *context, const uint8_t *message)
{
	// We have a packet!
	if (memcmp(message+4, "Synergy", 7)==0)
	{
		// Welcome message
		//		kMsgHello			= "Synergy%2i%2i"
		//		kMsgHelloBack		= "Synergy%2i%2i%s"
		sAddString(context, "Synergy");
		sAddUInt16(context, USYNERGY_PROTOCOL_MAJOR);
		sAddUInt16(context, USYNERGY_PROTOCOL_MINOR);
		sAddUInt32(context, (uint32_t)strlen(context->m_clientName));
		sAddString(context, context->m_clientName);
		if (!sSendReply(context))
		{
			// Send reply failed, let's try to reconnect
			sTrace(context, "SendReply failed, trying to reconnect in a second");
			context->m_connected = USYNERGY_FALSE;
			context->m_sleepFunc(context->m_cookie, 1000);
		}
		else
		{
			// Let's assume we're connected
			char buffer[256+1];
			sprintf(buffer, "Connected as client \"%s\"", context->m_clientName);
			sTrace(context, buffer);
			context->m_hasReceivedHello = USYNERGY_TRUE;
		}
		return;
	}
	else if (USYNERGY_IS_PACKET("QINF"))
	{
		// Screen info. Reply with DINF
		//		kMsgQInfo			= "QINF"
		//		kMsgDInfo			= "DINF%2i%2i%2i%2i%2i%2i%2i"
		uint16_t x = 0, y = 0, warp = 0;
		sAddString(context, "DINF");
		sAddUInt16(context, x);
		sAddUInt16(context, y);
		sAddUInt16(context, context->m_clientWidth);
		sAddUInt16(context, context->m_clientHeight);
		sAddUInt16(context, warp);
		sAddUInt16(context, 0);		// mx?
		sAddUInt16(context, 0);		// my?
		sSendReply(context);
		return;
	}
	else if (USYNERGY_IS_PACKET("CIAK"))
	{
		// Do nothing?
		//		kMsgCInfoAck		= "CIAK"
		return;
	}
	else if (USYNERGY_IS_PACKET("CROP"))
	{
		// Do nothing?
		//		kMsgCResetOptions	= "CROP"
		return;
	}
	else if (USYNERGY_IS_PACKET("CINN"))
	{
		// Screen enter. Reply with CNOP
		//		kMsgCEnter 			= "CINN%2i%2i%4i%2i"

		context->m_mouseX_old = sNetToNative16(message + 8);
		context->m_mouseY_old = sNetToNative16(message + 10);

		// Obtain the Synergy sequence number
		context->m_sequenceNumber = sNetToNative32(message + 12);
		context->m_isCaptured = USYNERGY_TRUE;

		// Call callback
		if (context->m_screenActiveCallback != 0L)
			context->m_screenActiveCallback(context->m_cookie, USYNERGY_TRUE);
	}
	else if (USYNERGY_IS_PACKET("COUT"))
	{
		// Screen leave
		//		kMsgCLeave 			= "COUT"
		context->m_isCaptured = USYNERGY_FALSE;

		// Call callback
		if (context->m_screenActiveCallback != 0L)
			context->m_screenActiveCallback(context->m_cookie, USYNERGY_FALSE);
	}
	else if (USYNERGY_IS_PACKET("DMDN"))
	{
		// Mouse down
		//		kMsgDMouseDown		= "DMDN%1i"
		char btn = message[8]-1;
		if (btn==2)
			context->m_mouseButtonRight		= USYNERGY_TRUE;
		else if (btn==1)
			context->m_mouseButtonMiddle	= USYNERGY_TRUE;
		else
			context->m_mouseButtonLeft		= USYNERGY_TRUE;
		sSendMouseDownCallback(context);
	}
	else if (USYNERGY_IS_PACKET("DMUP"))
	{
		// Mouse up
		//		kMsgDMouseUp		= "DMUP%1i"
		char btn = message[8]-1;
		if (btn==2)
			context->m_mouseButtonRight		= USYNERGY_FALSE;
		else if (btn==1)
			context->m_mouseButtonMiddle	= USYNERGY_FALSE;
		else
			context->m_mouseButtonLeft		= USYNERGY_FALSE;
		sSendMouseUpCallback(context);
	}
	else if (USYNERGY_IS_PACKET("DMMV"))
	{
		// Mouse move. Reply with CNOP
		//		kMsgDMouseMove		= "DMMV%2i%2i"
		context->m_mouseX = sNetToNative16(message+8);
		context->m_mouseY = sNetToNative16(message+10);
		sSendMouseMoveCallback(context);
	}
	else if (USYNERGY_IS_PACKET("DMWM"))
	{
		// Mouse wheel
		//		kMsgDMouseWheel		= "DMWM%2i%2i"
		//		kMsgDMouseWheel1_0	= "DMWM%2i"
		context->m_mouseWheelX += sNetToNative16(message+8);
		context->m_mouseWheelY += sNetToNative16(message+10);
		sSendMouseWheelCallback(context);
	}
	else if (USYNERGY_IS_PACKET("DKDN"))
	{
		// Key down
		//		kMsgDKeyDown		= "DKDN%2i%2i%2i"
		//		kMsgDKeyDown1_0		= "DKDN%2i%2i"
		//uint16_t id = sNetToNative16(message+8);
		uint16_t mod = sNetToNative16(message+10);
		uint16_t key = sNetToNative16(message+12);
		sSendKeyboardCallback(context, key, mod, USYNERGY_TRUE, USYNERGY_FALSE);
	}
	else if (USYNERGY_IS_PACKET("DKRP"))
	{
		// Key repeat
		//		kMsgDKeyRepeat		= "DKRP%2i%2i%2i%2i"
		//		kMsgDKeyRepeat1_0	= "DKRP%2i%2i%2i"
		uint16_t mod = sNetToNative16(message+10);
//		uint16_t count = sNetToNative16(message+12);
		uint16_t key = sNetToNative16(message+14);
		sSendKeyboardCallback(context, key, mod, USYNERGY_TRUE, USYNERGY_TRUE);
	}
	else if (USYNERGY_IS_PACKET("DKUP"))
	{
		// Key up
		//		kMsgDKeyUp			= "DKUP%2i%2i%2i"
		//		kMsgDKeyUp1_0		= "DKUP%2i%2i"
		//uint16 id=Endian::sNetToNative(sbuf[4]);
		uint16_t mod = sNetToNative16(message+10);
		uint16_t key = sNetToNative16(message+12);
		sSendKeyboardCallback(context, key, mod, USYNERGY_FALSE, USYNERGY_FALSE);
	}
	else if (USYNERGY_IS_PACKET("DGBT"))
	{
		// Joystick buttons
		//		kMsgDGameButtons	= "DGBT%1i%2i";
		uint8_t	joy_num = message[8];
		if (joy_num<USYNERGY_NUM_JOYSTICKS)
		{
			// Copy button state, then send callback
			context->m_joystickButtons[joy_num] = (message[9] << 8) | message[10];
			sSendJoystickCallback(context, joy_num);
		}
	}
	else if (USYNERGY_IS_PACKET("DGST"))
	{
		// Joystick sticks
		//		kMsgDGameSticks		= "DGST%1i%1i%1i%1i%1i";
		uint8_t	joy_num = message[8];
		if (joy_num<USYNERGY_NUM_JOYSTICKS)
		{
			// Copy stick state, then send callback
			memcpy(context->m_joystickSticks[joy_num], message+9, 4);
			sSendJoystickCallback(context, joy_num);
		}
	}
	else if (USYNERGY_IS_PACKET("DSOP"))
	{
		// Set options
		//		kMsgDSetOptions		= "DSOP%4I"
	}
	else if (USYNERGY_IS_PACKET("CALV"))
	{
		// Keepalive, reply with CALV and then CNOP
		//		kMsgCKeepAlive		= "CALV"
		sAddString(context, "CALV");
		sSendReply(context);
		// now reply with CNOP
	}
	else if (USYNERGY_IS_PACKET("DCLP"))
	{
		// Clipboard message
		//		kMsgDClipboard		= "DCLP%1i%4i%s"
		//
		// The clipboard message contains:
		//		1 uint32:	The size of the message
		//		4 chars: 	The identifier ("DCLP")
		//		1 uint8: 	The clipboard index
		//		1 uint32:	The sequence number. It's zero, because this message is always coming from the server?
		//		1 uint32:	The total size of the remaining 'string' (as per the Synergy %s string format (which is 1 uint32 for size followed by a char buffer (not necessarily null terminated)).
		//		1 uint32:	The number of formats present in the message
		// And then 'number of formats' times the following:
		//		1 uint32:	The format of the clipboard data
		//		1 uint32:	The size n of the clipboard data
		//		n uint8:	The clipboard data
		const uint8_t *	parse_msg	= message+17;
		uint32_t		num_formats = sNetToNative32(parse_msg);
		parse_msg += 4;
		for (; num_formats; num_formats--)
		{
			// Parse clipboard format header
			uint32_t format	= sNetToNative32(parse_msg);
			uint32_t size	= sNetToNative32(parse_msg+4);
			parse_msg += 8;

			// Call callback
			if (context->m_clipboardCallback)
				context->m_clipboardCallback(context->m_cookie, format, parse_msg, size);

			parse_msg += size;
		}
	}
	else
	{
		// Unknown packet, could be any of these
		//		kMsgCNoop 			= "CNOP"
		//		kMsgCClose 			= "CBYE"
		//		kMsgCClipboard 		= "CCLP%1i%4i"
		//		kMsgCScreenSaver 	= "CSEC%1i"
		//		kMsgDKeyRepeat		= "DKRP%2i%2i%2i%2i"
		//		kMsgDKeyRepeat1_0	= "DKRP%2i%2i%2i"
		//		kMsgDMouseRelMove	= "DMRM%2i%2i"
		//		kMsgEIncompatible	= "EICV%2i%2i"
		//		kMsgEBusy 			= "EBSY"
		//		kMsgEUnknown		= "EUNK"
		//		kMsgEBad			= "EBAD"
		char buffer[64];
		sprintf(buffer, "Unknown packet '%c%c%c%c'", message[4], message[5], message[6], message[7]);
		printf("Unknown packet '%c%c%c%c'\n", message[4], message[5], message[6], message[7]);
		sTrace(context, buffer);
		return;
	}

	// Reply with CNOP maybe?
	sAddString(context, "CNOP");
	sSendReply(context);
}
#undef USYNERGY_IS_PACKET


static sSetMachineState(uSynergyContext *context)
{
	context->m_clientName		= "Android";
	context->m_clientWidth		= 1024;
	context->m_clientHeight		= 600;
	context->m_connectFunc		= uSynergyConnectFunc;
	context->m_receiveFunc		= uSynergyReceiveFunc;
	context->m_sendFunc			= uSynergySendFunc;
	context->m_getTimeFunc		= uSynergyGetTimeFunc;
	context->m_connectDevice	= uSynergyConnectDevice;
	context->m_screenActiveCallback = uSynergyScreenActiveCallback;
	context->m_mouseMoveCallback	= uSynergyMouseMoveCallback;
	context->m_mouseUpCallback	= uSynergyMouseUpCallback;
	context->m_mouseDownCallback= uSynergyMouseDownCallback;
	context->m_mouseWheelCallback	= uSynergyMouseWheelCallback;
	context->m_keyboardCallback	= uSynergyKeyboardCallback;
	context->m_sleepFunc		= uSynergySleepFunc;
}

/**
@brief Mark context as being disconnected
**/
static void sSetDisconnected(uSynergyContext *context)
{
	context->m_connected		= USYNERGY_FALSE;
	context->m_hasReceivedHello = USYNERGY_FALSE;
	context->m_isCaptured		= USYNERGY_FALSE;
	context->m_replyCur			= context->m_replyBuffer + 4;
	context->m_sequenceNumber	= 0;
	suinput_close(context->m_cookie->uinput_mouse);
}



/**
@brief Update a connected context
**/
static void sUpdateContext(uSynergyContext *context)
{
	/* Receive data (blocking) */
	int receive_size = USYNERGY_RECEIVE_BUFFER_SIZE - context->m_receiveOfs;
	int num_received = 0;
	int packlen = 0;
	if (context->m_receiveFunc(context->m_cookie, context->m_receiveBuffer+context->m_receiveOfs, receive_size, &num_received) == USYNERGY_FALSE)
	{
		/* Receive failed, let's try to reconnect */
		char buffer[128];
		sprintf(buffer, "Receive failed (%d bytes asked, %d bytes received), trying to reconnect in a second", receive_size, num_received);
		sTrace(context, buffer);
		sSetDisconnected(context);
		context->m_sleepFunc(context->m_cookie, 1000);
		return;
	}

	context->m_receiveOfs += num_received;

	/*	If we didn't receive any data then we're probably still polling to get connected and
		therefore not getting any data back. To avoid overloading the system with a Synergy
		thread that would hammer on polling, we let it rest for a bit if there's no data. */
	if (num_received == 0)
		context->m_sleepFunc(context->m_cookie, 500);

	/* Check for timeouts */
	if (context->m_hasReceivedHello)
	{
		uint32_t cur_time = context->m_getTimeFunc();
		if (num_received == 0)
		{
			/* Timeout after 2 secs of inactivity (we received no CALV) */
			if ((cur_time - context->m_lastMessageTime) > USYNERGY_IDLE_TIMEOUT)
				sSetDisconnected(context);
		}
		else
			context->m_lastMessageTime = cur_time;
	}

	/* Eat packets */
	for (;;)
	{
		/* Grab packet length and bail out if the packet goes beyond the end of the buffer */
		packlen = sNetToNative32(context->m_receiveBuffer);
		if (packlen+4 > context->m_receiveOfs)
			break;
		/* Process message */
		sProcessMessage(context, context->m_receiveBuffer);
		/* Move packet to front of buffer */
		memmove(context->m_receiveBuffer, context->m_receiveBuffer+packlen+4, context->m_receiveOfs-packlen-4);
		context->m_receiveOfs -= packlen+4;
	}

	/* Throw away over-sized packets */
	if (packlen > USYNERGY_RECEIVE_BUFFER_SIZE)
	{
		/* Oversized packet, ditch tail end */
		char buffer[128];
		sprintf(buffer, "Oversized packet: '%c%c%c%c' (length %d)", context->m_receiveBuffer[4], context->m_receiveBuffer[5], context->m_receiveBuffer[6], context->m_receiveBuffer[7], packlen);
		printf("Oversized packet: '%c%c%c%c' (length %d)\n", context->m_receiveBuffer[4], context->m_receiveBuffer[5], context->m_receiveBuffer[6], context->m_receiveBuffer[7], packlen);
		sTrace(context, buffer);
		num_received = context->m_receiveOfs-4; // 4 bytes for the size field
		while (num_received != packlen)
		{
			int buffer_left = packlen - num_received;
			int to_receive = buffer_left < USYNERGY_RECEIVE_BUFFER_SIZE ? buffer_left : USYNERGY_RECEIVE_BUFFER_SIZE;
			int ditch_received = 0;
			if (context->m_receiveFunc(context->m_cookie, context->m_receiveBuffer, to_receive, &ditch_received) == USYNERGY_FALSE)
			{
				/* Receive failed, let's try to reconnect */
				sTrace(context, "Receive failed, trying to reconnect in a second");
				sSetDisconnected(context);
				context->m_sleepFunc(context->m_cookie, 1000);
				break;
			}
			else
			{
				num_received += ditch_received;
			}
		}
		context->m_receiveOfs = 0;
	}
}


//---------------------------------------------------------------------------------------------------------------------
//	Public interface
//---------------------------------------------------------------------------------------------------------------------



/**
@brief Initialize uSynergy context
**/
void uSynergyInit(uSynergyContext *context)
{
	/* Zero memory */
	memset(context, 0, sizeof(uSynergyContext));

	CookieType *cookie;
	cookie = malloc(sizeof(CookieType));
	memset(cookie, 0, sizeof(CookieType));
	context->m_cookie = cookie;

	/* Initialize to default state */
	sSetMachineState(context);
	sSetDisconnected(context);
}


/**
@brief Update uSynergy
**/
void uSynergyUpdate(uSynergyContext *context)
{
	if (context->m_connected)
	{
		/* Update context, receive data, call callbacks */
		sUpdateContext(context);
	}
	else
	{
		/* Try to connect */
		if (context->m_connectFunc(context->m_cookie)) {
			context->m_connected = USYNERGY_TRUE;
			context->m_connectDevice(context->m_cookie, context->m_clientWidth, context->m_clientHeight);
		}
	}
}



/**
@brief Send clipboard data
**/
void uSynergySendClipboard(uSynergyContext *context, const char *text)
{
	// Calculate maximum size that will fit in a reply packet
	uint32_t overhead_size =	4 +					/* Message size */
								4 +					/* Message ID */
								1 +					/* Clipboard index */
								4 +					/* Sequence number */
								4 +					/* Rest of message size (because it's a Synergy string from here on) */
								4 +					/* Number of clipboard formats */
								4 +					/* Clipboard format */
								4;					/* Clipboard data length */
	uint32_t max_length = USYNERGY_REPLY_BUFFER_SIZE - overhead_size;

	// Clip text to max length
	uint32_t text_length = (uint32_t)strlen(text);
	if (text_length > max_length)
	{
		char buffer[128];
		sprintf(buffer, "Clipboard buffer too small, clipboard truncated at %d characters", max_length);
		sTrace(context, buffer);
		text_length = max_length;
	}

	// Assemble packet
	sAddString(context, "DCLP");
	sAddUInt8(context, 0);							/* Clipboard index */
	sAddUInt32(context, context->m_sequenceNumber);
	sAddUInt32(context, 4+4+4+text_length);			/* Rest of message size: numFormats, format, length, data */
	sAddUInt32(context, 1);							/* Number of formats (only text for now) */
	sAddUInt32(context, USYNERGY_CLIPBOARD_FORMAT_TEXT);
	sAddUInt32(context, text_length);
	sAddString(context, text);
	sSendReply(context);
}

/**
@brief Connect function

This function is called when uSynergy needs to connect to the host. It doesn't imply a network implementation or
destination address, that must all be handled on the user side. The function should return USYNERGY_TRUE if a
connection was established or USYNERGY_FALSE if it could not connect.

When network errors occur (e.g. uSynergySend or uSynergyReceive fail) then the connect call will be called again
so the implementation of the function must close any old connections and clean up resources before retrying.

@param cookie		Cookie supplied in the Synergy context
**/
uSynergyBool uSynergyConnectFunc(uSynergyCookie cookie)
{
	memset(&(cookie->server_addr), 0, sizeof(struct sockaddr));
	cookie->server_addr.sin_family = AF_INET;
	cookie->server_addr.sin_port = htons(24800);
	cookie->server_addr.sin_addr.s_addr = inet_addr("10.11.71.124");
	//cookie->server_addr.sin_addr.s_addr = inet_addr("192.168.0.105");

	cookie->sockfd = socket(AF_INET, SOCK_STREAM, 0);
	socklen_t addr_len;

	if (connect(cookie->sockfd, (struct sockaddr *)& cookie->server_addr, sizeof(struct sockaddr)) == 0) {
		return USYNERGY_TRUE;
	} else {
		perror("connect error.");
		return USYNERGY_FALSE;
	}
}

/**
@brief Receive function

This function is called when uSynergy needs to receive data from the default connection. It should return
USYNERGY_TRUE if receiving data succeeded and USYNERGY_FALSE otherwise. This function should block until data
has been received and wait for data to become available. If @a outLength is set to 0 upon completion it is
assumed that the connection is alive, but still in a connecting state and needs time to settle.

@param cookie		Cookie supplied in the Synergy context
@param buffer		Address of buffer to receive data into
@param maxLength	Maximum amount of bytes to write into the receive buffer
@param outLength	Address of integer that receives the actual amount of bytes written into @a buffer
**/
uSynergyBool uSynergyReceiveFunc(uSynergyCookie cookie, uint8_t *buffer, int maxLength, int* outLength)
{
	int ret;
	ret = recv(cookie->sockfd, buffer, maxLength, 0);
	if (ret <= 0)
		return USYNERGY_FALSE;

	*outLength = ret;
	return USYNERGY_TRUE;
}

/**
@brief Send function

This function is called when uSynergy needs to send something over the default connection. It should return
USYNERGY_TRUE if sending succeeded and USYNERGY_FALSE otherwise. This function should block until the send
operation is completed.

@param cookie       Cookie supplied in the Synergy context
@param buffer       Address of buffer to send
@param length       Length of buffer to send
**/
uSynergyBool uSynergySendFunc(uSynergyCookie cookie, const uint8_t *buffer, int length)
{
	int ret;
	ret = send(cookie->sockfd, buffer, length, 0);
	if (ret < 0 || ret != length)
		return USYNERGY_FALSE;

	return USYNERGY_TRUE;
}


/**
@brief Get time function

This function is called when uSynergy needs to know the current time. This is used to determine when timeouts
have occured. The time base should be a cyclic millisecond time value.

@returns            Time value in milliseconds
**/
uint32_t uSynergyGetTimeFunc()
{
	uint32_t ret = 0;
	time_t now;

	ret = time(&now);

	return ret*1000;
}

#define BUS_VIRTUAL 0x06
uSynergyBool uSynergyConnectDevice(uSynergyCookie cookie, int clientWidth, int clientHeight)
{
	struct input_id device_id;
	device_id.bustype = BUS_VIRTUAL;
	device_id.vendor  = 1;
	device_id.product = 1;
	device_id.version = 0x0100;

	cookie->uinput_keyboard = suinput_open("qwerty", &(cookie->device_id), keyboard);
	cookie->uinput_mouse = suinput_open("synergy-mouse", &(cookie->device_id), mouse);
	return USYNERGY_TRUE;
}

/**
@brief Screen active callback

This callback is called when Synergy makes the screen active or inactive. This
callback is usually sent when the mouse enters or leaves the screen.

@param cookie       Cookie supplied in the Synergy context
@param active       Activation flag, 1 if the screen has become active, 0 if the screen has become inactive
**/
void uSynergyScreenActiveCallback(uSynergyCookie cookie, uSynergyBool active)
{
	if (active) {
		/* Screen ative */
	} else {

	}
}

/**
@brief Mouse callback

This callback is called when a mouse events happens. The mouse X and Y position,
wheel and button state is communicated in the message. It's up to the user to
interpret if this is a mouse up, down, double-click or other message.

@param cookie       Cookie supplied in the Synergy context
@param x            Mouse X position
@param y            Mouse Y position
@param wheelX       Mouse wheel X position
@param wheelY       Mouse wheel Y position
@param buttonLeft   Left button pressed status, 0 for released, 1 for pressed
@param buttonMiddle Middle button pressed status, 0 for released, 1 for pressed
@param buttonRight  Right button pressed status, 0 for released, 1 for pressed
**/
uSynergyBool uSynergyMouseMoveCallback(uSynergyCookie cookie, int32_t x, int32_t y)
{
	int ret;
	ret = suinput_move_pointer(cookie->uinput_mouse, x, y);
	return USYNERGY_TRUE;
}

uSynergyBool uSynergyMouseUpCallback(uSynergyCookie cookie, uSynergyBool buttonLeft, uSynergyBool buttonRight, uSynergyBool buttonMiddle)
{
	int ret;
	if (!buttonLeft)
		ret = suinput_release(cookie->uinput_mouse, BTN_LEFT);

	if (!buttonRight)
		ret = suinput_release(cookie->uinput_mouse, BTN_RIGHT);

	if (!buttonMiddle)
		ret = suinput_release(cookie->uinput_mouse, BTN_MIDDLE);

	return USYNERGY_TRUE;
}
uSynergyBool uSynergyMouseDownCallback(uSynergyCookie cookie, uSynergyBool buttonLeft, uSynergyBool buttonRight, uSynergyBool buttonMiddle)
{
	int ret;
	if (buttonLeft)
		ret = suinput_press(cookie->uinput_mouse, BTN_LEFT);

	if (buttonRight)
		ret = suinput_press(cookie->uinput_mouse, BTN_RIGHT);

	if (buttonMiddle)
		ret = suinput_press(cookie->uinput_mouse, BTN_MIDDLE);

	return USYNERGY_TRUE;
}
uSynergyBool uSynergyMouseWheelCallback(uSynergyCookie cookie, int16_t wheelX, int16_t wheelY)
{
	return USYNERGY_TRUE;
}

/**
@brief Key event callback

This callback is called when a key is pressed or released.

@param cookie       Cookie supplied in the Synergy context
@param key          Key code of key that was pressed or released
@param modifiers    Status of modifier keys (alt, shift, etc.)
@param down         Down or up status, 1 is key is pressed down, 0 if key is released (up)
@param repeat       Repeat flag, 1 if the key is down because the key is repeating, 0 if the key is initially pressed by the user
**/
void uSynergyKeyboardCallback(uSynergyCookie cookie, uint16_t key, uint16_t modifiers, uSynergyBool down, uSynergyBool repeat)
{
	int ret;
	if (down)
		ret = suinput_press(cookie->uinput_keyboard, key);
	else
		ret = suinput_release(cookie->uinput_keyboard, key);
}

/**
@brief Thread sleep function

This function is called when uSynergy wants to suspend operation for a while before retrying an operation. It
is mostly used when a socket times out or disconnect occurs to prevent uSynergy from continuously hammering a
network connection in case the network is down.

@param cookie       Cookie supplied in the Synergy context
@param timeMs       Time to sleep the current thread (in milliseconds)
**/
void uSynergySleepFunc(uSynergyCookie cookie, int timeMs)
{
	msleep(timeMs);
}
