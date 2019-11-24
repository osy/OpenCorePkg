/** @file
  This protocol provides message definitions for HECI communucation.
  This header is based on DCMI-HI: DCMI Host Interface Specification:
  https://www.intel.com/content/dam/www/public/us/en/documents/technical-specifications/dcmi-hi-1-0-spec.pdf

  Copyright (c) 2019, vit9696. All rights reserved.<BR>
  SPDX-License-Identifier: BSD-2-Clause
**/

#ifndef HECI_MSG_H
#define HECI_MSG_H

//
// All HECI bus messages are implemented using the HECI message format described in
// section 6.1; The HostAddress and MEAddress fields in the HECI_MESSAGE_HEADER
// will be 0x0 and 0x0, respectively.
//
#define HBM_HOST_ADDRESS   0x00
#define HBM_ME_ADDRESS     0x00
#define HBM_CLIENT_ADDRESS 0x01

//
// See: Table 9-2 - HECI Bus Message Command Summary, HECI Version 0x0001
// Page 76
//
#define HOST_VERSION_REQUEST            0x01
#define HOST_STOP_REQUEST               0x02
#define ME_STOP_REQUEST                 0x03
#define HOST_ENUMERATION_REQUEST        0x04
#define HOST_CLIENT_PROPERTIES_REQUEST  0x05
#define CLIENT_CONNECT_REQUEST          0x06
#define CLIENT_DISCONNECT_REQUEST       0x07
#define FLOW_CONTROL                    0x08
#define CLIENT_CONNECTION_RESET_REQUEST 0x09

#pragma pack(1)

//
// See: 7.2 General Format of HECI Bus Messages
// Page 44
//

typedef union {
  UINT8 Data;
  struct {
    UINT8 Command    : 7;
    UINT8 IsResponse : 1;
  } Fields;
} HBM_COMMAND;

typedef struct {
  HBM_COMMAND Command;
  UINT8 CommandSpecificData[];
} HECI_BUS_MESSAGE;

//
// HOST_VERSION_REQUEST
// See: 7.6 Host Version Request
// Page 45
//

typedef struct {
  UINT8 MinorVersion;
  UINT8 MajorVersion;
} HBM_VERSION;

typedef struct {
  HBM_COMMAND Command;
  UINT8       Reserved;
  HBM_VERSION HostVersion;
} HBM_HOST_VERSION_REQUEST;

typedef struct {
  HBM_COMMAND Command;
  UINT8       HostVersionSupported;
  HBM_VERSION MeMaxVersion;
} HBM_HOST_VERSION_RESPONSE;

typedef union {
  HBM_HOST_VERSION_REQUEST  Request;
  HBM_HOST_VERSION_RESPONSE Response;
} HBM_HOST_VERSION_BUFFER;

//
// HOST_STOP_REQUEST
// See: 7.8 Host Stop Request
// Page 48
//

typedef struct {
  HBM_COMMAND Command;
  UINT8       Reason;
  UINT8       Reserved[2];
} HBM_HOST_STOP_REQUEST;

typedef struct {
  HBM_COMMAND Command;
  UINT8       Reserved[3];
} HBM_HOST_STOP_RESPONSE;

typedef union {
  HBM_HOST_STOP_REQUEST  Request;
  HBM_HOST_STOP_RESPONSE Response;
} HBM_HOST_STOP_BUFFER;

//
// ME_STOP_REQUEST
// See: 7.10 ME Stop Request
// Page 49
//

typedef struct {
  HBM_COMMAND Command;
  UINT8       Reason;
  UINT8       Reserved[2];
} HBM_ME_STOP_REQUEST;

//
// HOST_ENUMERATION_REQUEST
// See: 7.11 Host Enumeration Request
// Page 49
//

typedef struct {
  HBM_COMMAND Command;
  UINT8       Reserved[3];
} HBM_HOST_ENUMERATION_REQUEST;

typedef struct {
  HBM_COMMAND Command;
  UINT8       Reserved[3];
  UINT8       ValidAddresses[32];
} HBM_HOST_ENUMERATION_RESPONSE;

#define HBM_ME_CLIENT_MAX 256

typedef union {
  HBM_HOST_ENUMERATION_REQUEST  Request;
  HBM_HOST_ENUMERATION_RESPONSE Response;
} HBM_HOST_ENUMERATION_BUFFER;

//
// HOST_CLIENT_PROPERTIES_REQUEST
// See: 7.13 Host Client Properties Request
// Page 52
//

typedef struct {
  HBM_COMMAND Command;
  UINT8       Address;
  UINT8       Reserved[2];
} HBM_HOST_CLIENT_PROPERTIES_REQUEST;

typedef struct {
  EFI_GUID ProtocolName;
  UINT8    ProtocolVersion;
  UINT8    MaxNumberOfConnections;
  UINT8    FixedAddress;
  UINT8    SingleReceiveBuffer;
  UINT32   MaxMessageLength;
} HECI_CLIENT_PROPERTIES;

typedef struct {
  HBM_COMMAND            Command;
  UINT8                  Address;
  UINT8                  Status;
  UINT8                  Reserved;
  HECI_CLIENT_PROPERTIES ClientProperties;
} HBM_HOST_CLIENT_PROPERTIES_RESPONSE;

typedef union {
  HBM_HOST_CLIENT_PROPERTIES_REQUEST  Request;
  HBM_HOST_CLIENT_PROPERTIES_RESPONSE Response;
} HBM_HOST_CLIENT_PROPERTIES_BUFFER;

//
// CLIENT_CONNECT_REQUEST
// See: 7.15 Client Connect Request
// Page 54
//

typedef struct {
  HBM_COMMAND Command;
  UINT8       MeAddress;
  UINT8       HostAddress;
  UINT8       Reserved;
} HBM_CLIENT_CONNECT_REQUEST;

typedef struct {
  HBM_COMMAND Command;
  UINT8       MeAddress;
  UINT8       HostAddress;
  UINT8       Status;
} HBM_CLIENT_CONNECT_RESPONSE;

typedef union {
  HBM_CLIENT_CONNECT_REQUEST  Request;
  HBM_CLIENT_CONNECT_RESPONSE Response;
} HBM_CLIENT_CONNECT_BUFFER;

#define HBM_CLIENT_CONNECT_SUCCESS            0x00
#define HBM_CLIENT_CONNECT_NOT_FOUND          0x01
#define HBM_CLIENT_CONNECT_ALREADY_CONNECTED  0x02
#define HBM_CLIENT_CONNECT_OUT_OF_RESOURCES   0x03
#define HBM_CLIENT_CONNECT_INVALID_PARAMETER  0x04

//
// CLIENT_DISCONNECT_REQUEST
// See: 7.17 Client Disconnect Request
// Page 56
//

typedef struct {
  HBM_COMMAND Command;
  UINT8       MeAddress;
  UINT8       HostAddress;
  UINT8       Reserved;
} HBM_CLIENT_DISCONNECT_REQUEST;

typedef struct {
  HBM_COMMAND Command;
  UINT8       MeAddress;
  UINT8       HostAddress;
  UINT8       Status;
} HBM_CLIENT_DISCONNECT_RESPONSE;

typedef union {
  HBM_CLIENT_DISCONNECT_REQUEST  Request;
  HBM_CLIENT_DISCONNECT_RESPONSE Response;
} HBM_CLIENT_DISCONNECT_BUFFER;

//
// FLOW_CONTROL
// See: 7.19 Flow Control
// Page 57
//

typedef struct {
  HBM_COMMAND Command;
  UINT8       MeAddress;
  UINT8       HostAddress;
  UINT8       Reserved[5];
} HBM_FLOW_CONTROL;

//
// HBM_CLIENT_CONNECTION_RESET_REQUEST
// See: 7.20 Client Connection Reset Request
// Page 58
//

typedef struct {
  HBM_COMMAND Command;
  UINT8       MEAddress;
  UINT8       HostAddress;
  UINT8       Reserved;
} HBM_CLIENT_CONNECTION_RESET_REQUEST;

typedef struct {
  HBM_COMMAND Command;
  UINT8       MEAddress;
  UINT8       HostAddress;
  UINT8       Status;
} HBM_CLIENT_CONNECTION_RESET_RESPONSE;

typedef union {
  HBM_CLIENT_CONNECTION_RESET_REQUEST  Request;
  HBM_CLIENT_CONNECTION_RESET_RESPONSE Response;
} HBM_CLIENT_CONNECTION_RESET_BUFFER;

//
// Select known clients
//

//
// FBF6FCF1-96CF-4E2E-A6A6-1BAB8CBE36B1
//
#define ME_EPID_PROTOCOL_GUID \
  { 0xFBF6FCF1, 0x96CF, 0x4E2E, { 0xA6, 0xA6, 0x1B, 0xAB, 0x8C, 0xBE, 0x36, 0xB1 } }

extern EFI_GUID gMeEpidProtocolGuid;

#define EPID_PROVISION_COMMAND 0x10005U

#define EPID_PROVISION_REQUEST_SUBCOMMAND 0x0
#define EPID_PROVISION_PERFORM_SUBCOMMAND 0x1

#define EPID_CERTIFICATE_SIZE      876U
#define EPID_GROUP_PUBLIC_KEY_SIZE 392U

//
// EPID_PROVISION_REQUEST_SUBCOMMAND
//

typedef struct {
  UINT32  Command;
  UINT32  Subcommand;
  UINT32  Reserved[2];
} EPID_PROVISION_REQUEST_REQUEST;

typedef struct {
  UINT32  Command;
  UINT32  Subcommand;
  UINT32  Reserved[2];
  UINT32  Status;
  UINT32  Key;
} EPID_PROVISION_REQUEST_RESPONSE;

typedef union {
  EPID_PROVISION_REQUEST_REQUEST   Request;
  EPID_PROVISION_REQUEST_RESPONSE  Response;
} EPID_PROVISION_REQUEST_BUFFER;

#define EPID_STATUS_PROVISIONED    0x00
#define EPID_STATUS_CAN_PROVISION  0x02
#define EPID_STATUS_FAIL_PROVISION 0x03

//
// EPID_PROVISION_PERFORM_SUBCOMMAND
//

typedef struct {
  UINT32  Command;
  UINT32  Subcommand;
  UINT32  Reserved;
  UINT32  PayloadSize;
  UINT8   Certificate[EPID_CERTIFICATE_SIZE];
  UINT8   PublicKey[EPID_GROUP_PUBLIC_KEY_SIZE];
} EPID_PROVISION_PERFORM_REQUEST;

typedef struct {
  UINT32  Command;
  UINT32  Subcommand;
  UINT32  Status;
  UINT32  Reserved;
} EPID_PROVISION_PERFORM_RESPONSE;

typedef union {
  EPID_PROVISION_PERFORM_REQUEST   Request;
  EPID_PROVISION_PERFORM_RESPONSE  Response;
} EPID_PROVISION_PERFORM_BUFFER;

#define EPID_PROVISION_PERFORM_PAYLOAD_SIZE (EPID_CERTIFICATE_SIZE + EPID_GROUP_PUBLIC_KEY_SIZE)

#define ME_FPF_PROTOCOL_GUID \
  { 0x3893448C, 0xEAB6, 0x4F4C, { 0xB2, 0x3C, 0x57, 0xC2, 0xC4, 0x65, 0x8D, 0xFC } }

extern EFI_GUID gMeFpfProtocolGuid;

#pragma pack()

#endif // HECI_MSG_H
