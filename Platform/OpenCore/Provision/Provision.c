/** @file
  This file implements provisioning.

  Copyright (c) 2019, vit9696. All rights reserved.<BR>
  Portions copyright (c) 2019, savvas. All rights reserved.<BR>
  SPDX-License-Identifier: BSD-2-Clause
**/

#include <PiDxe.h>
#include <Protocol/FirmwareVolume.h>
#include <Protocol/PciRootBridgeIo.h>
#include <Library/UefiBootServicesTableLib.h>
#include <Library/UefiRuntimeServicesTableLib.h>
#include <Library/BaseLib.h>
#include <Library/BaseMemoryLib.h>
#include <Library/MemoryAllocationLib.h>
#include <Library/PciLib.h>
#include <Library/HobLib.h>
#include <Library/IoLib.h>
#include <Guid/GlobalVariable.h>
#include <OpenCore.h>

#include <Library/OcMiscLib.h>

#include <Protocol/Heci.h>
#include <IndustryStandard/AppleProvisioning.h>
#include <IndustryStandard/HeciMsg.h>
#include <IndustryStandard/HeciClientMsg.h>

#define FORCE_PROVISIONING 1

STATIC UINT8 mCurrentMeClientRequestedReceiveMsg;
STATIC UINT8 mCurrentMeClientCanReceiveMsg;
STATIC UINT8 mCurrentMeClientAddress;

STATIC UINT8 mMeClientMap[HBM_ME_CLIENT_MAX];
STATIC UINT8 mMeClientActiveCount;
STATIC EFI_HECI_PROTOCOL *mHeci;
STATIC BOOLEAN mSendingHeciCommand;
STATIC BOOLEAN mSendingHeciCommandPerClient;


// Note: ready
STATIC
EFI_STATUS
ReadProvisioningDataFile (
  IN  EFI_GUID         *FvNameGuid,
  OUT VOID             **Buffer,
  OUT UINTN            *BufferSize
  )
{
  UINTN                         Index;
  EFI_STATUS                    Status;
  UINT32                        AuthenticationStatus;
  EFI_FIRMWARE_VOLUME_PROTOCOL  *FirmwareVolumeInterface;
  UINTN                         NumOfHandles;
  EFI_HANDLE                    *HandleBuffer;

  Status = gBS->LocateHandleBuffer (
    ByProtocol,
    &gEfiFirmwareVolumeProtocolGuid,
    NULL,
    &NumOfHandles,
    &HandleBuffer
    );

  if (!EFI_ERROR (Status)) {
    for (Index = 0; Index < NumOfHandles; ++Index) {
      Status = gBS->HandleProtocol (
        HandleBuffer[Index],
        &gEfiFirmwareVolumeProtocolGuid,
        (VOID **) &FirmwareVolumeInterface
        );

      if (EFI_ERROR (Status)) {
        gBS->FreePool (HandleBuffer);
        return Status;
      }

      *Buffer = NULL;
      *BufferSize = 0;

      Status = FirmwareVolumeInterface->ReadSection (
        FirmwareVolumeInterface,
        FvNameGuid,
        EFI_SECTION_RAW,
        0,
        Buffer,
        BufferSize,
        &AuthenticationStatus
        );

      if (!EFI_ERROR (Status)) {
        gBS->FreePool (HandleBuffer);
        return Status;
      }
    }

    gBS->FreePool (HandleBuffer);
    Status = EFI_NOT_FOUND;
  }

  //
  // Implement fallback for our firmwares.
  //
  if (EFI_ERROR (Status)) {
    DEBUG ((DEBUG_INFO, "OC: No %g in firmware, using default - %r\n", FvNameGuid, Status));

    if (CompareGuid (&gAppleEpidCertificateFileGuid, FvNameGuid)) {
      *Buffer = AllocateCopyPool (gDefaultAppleEpidCertificateSize, gDefaultAppleEpidCertificate);
      *BufferSize = gDefaultAppleEpidCertificateSize;
    } else if (CompareGuid (&gAppleEpidGroupPublicKeysFileGuid, FvNameGuid)) {
      *Buffer = AllocateCopyPool (gDefaultAppleGroupPublicKeysSize, gDefaultAppleGroupPublicKeys);
      *BufferSize = gDefaultAppleGroupPublicKeysSize;
    } else {
      *Buffer = NULL;
    }

    if (*Buffer != NULL) {
      Status = EFI_SUCCESS;
    } else {
      Status = EFI_NOT_FOUND;
    }
  }

  return Status;
}

// Note: ready
STATIC
EFI_STATUS
ReadProvisioningData (
  OUT EPID_CERTIFICATE       **EpidCertificate,
  OUT EPID_GROUP_PUBLIC_KEY  **EpidGroupPublicKeys,
  OUT UINT32 *EpidGroupPublicKeysCount
  )
{
  EFI_STATUS  Status;
  UINTN       EpidCertificateSize;
  UINTN       EpidGroupPublicKeysSize;

  Status = ReadProvisioningDataFile (
    &gAppleEpidCertificateFileGuid,
    (VOID **) EpidCertificate,
    &EpidCertificateSize
    );

  if (EFI_ERROR (Status)) {
    return Status;
  }

  Status = ReadProvisioningDataFile (
    &gAppleEpidGroupPublicKeysFileGuid,
    (VOID **) EpidGroupPublicKeys,
    &EpidGroupPublicKeysSize
    );

  if (EFI_ERROR (Status)) {
    gBS->FreePool (*EpidGroupPublicKeys);
    return Status;
  }

  if (EpidCertificateSize == EPID_CERTIFICATE_SIZE
    && EpidGroupPublicKeysSize % EPID_GROUP_PUBLIC_KEY_SIZE == 0) {
    *EpidGroupPublicKeysCount = EpidGroupPublicKeysSize / EPID_GROUP_PUBLIC_KEY_SIZE;
    return EFI_SUCCESS;
  }

  gBS->FreePool (*EpidCertificate);
  gBS->FreePool (*EpidGroupPublicKeys);
  return EFI_VOLUME_CORRUPTED;
}

// Note: ready
STATIC
EFI_STATUS
HeciLocateProtocol (
  VOID
  )
{
  EFI_STATUS  Status;

  if (mHeci != NULL) {
    return EFI_SUCCESS;
  }

  Status = gBS->LocateProtocol (
    &gEfiHeciProtocolGuid,
    NULL,
    (VOID **) &mHeci
    );

  if (EFI_ERROR (Status)) {
    DEBUG ((DEBUG_INFO, "OC: Failed to find HECI protocol - %r\n", Status));
  }

  return Status;
}

// Note: ready
STATIC
VOID
HeciUpdateReceiveMsgStatus (
  VOID
  )
{
  EFI_STATUS        Status;
  UINT32            Size;
  HBM_FLOW_CONTROL  Command;

  if (mSendingHeciCommandPerClient) {
    ZeroMem (&Command, sizeof (Command));
    Size = sizeof (Command);
    Status = mHeci->ReadMsg (
      BLOCKING,
      (UINT32 *) &Command,
      &Size
      );

    if (!EFI_ERROR (Status) && Command.Command.Fields.Command == FLOW_CONTROL) {
      ++mCurrentMeClientCanReceiveMsg;
    }
  }
}

// Note: ready
STATIC
EFI_STATUS
HeciGetResponse (
  OUT VOID    *MessageData,
  IN  UINT32  ResponseSize
  )
{
  EFI_STATUS        Status;
  HBM_FLOW_CONTROL  Command;
  BOOLEAN           SendingPerClient;

  Status = EFI_NOT_READY;

  OC_STATIC_ASSERT (sizeof (HBM_FLOW_CONTROL) == 8, "Invalid ME command size");

  if (mSendingHeciCommandPerClient || mSendingHeciCommand) {
    ZeroMem (MessageData, ResponseSize);

    SendingPerClient = mSendingHeciCommandPerClient;
    if (mSendingHeciCommandPerClient && !mCurrentMeClientCanReceiveMsg) {
      HeciUpdateReceiveMsgStatus ();
      // FIXME: This looks broken to me.
      SendingPerClient = mSendingHeciCommandPerClient;
    }

    if (SendingPerClient) {
      if (!mCurrentMeClientRequestedReceiveMsg) {
        ZeroMem (&Command, sizeof (Command));
        Command.Command.Fields.Command = FLOW_CONTROL;
        Command.MeAddress              = mCurrentMeClientAddress;
        Command.HostAddress            = HBM_CLIENT_ADDRESS;

        Status = mHeci->SendMsg (
          (UINT32 *) &Command,
          sizeof (Command),
          HBM_HOST_ADDRESS,
          HBM_ME_ADDRESS
          );

        if (!EFI_ERROR (Status)) {
          ++mCurrentMeClientRequestedReceiveMsg;
        }
      }
    }

    Status = mHeci->ReadMsg (
      BLOCKING,
      MessageData,
      &ResponseSize
      );

    if (!EFI_ERROR (Status)) {
      --mCurrentMeClientRequestedReceiveMsg;
    }
  }

  return Status;
}

// Note: ready
STATIC
EFI_STATUS
HeciSendMessageWithResponse (
  IN OUT VOID    *MessageData,
  IN     UINT32  RequestSize,
  IN     UINT32  ResponseSize
  )
{
  HECI_BUS_MESSAGE  *Message;
  HBM_COMMAND       Command;
  EFI_STATUS        Status;

  mSendingHeciCommand = TRUE;

  Message = (HECI_BUS_MESSAGE *) MessageData;
  Command = Message->Command;

  Status = mHeci->SendMsg (
    MessageData,
    RequestSize,
    HBM_HOST_ADDRESS,
    HBM_ME_ADDRESS
    );

  if (!EFI_ERROR (Status)) {
    Status = HeciGetResponse (MessageData, ResponseSize);

    if (!EFI_ERROR (Status)
      && Command.Fields.Command != Message->Command.Fields.Command) {
      Status = EFI_PROTOCOL_ERROR;
    }
  }

  mSendingHeciCommand = FALSE;

  return Status;
}

// Note: ready
STATIC
EFI_STATUS
HeciGetClientMap (
  OUT UINT8  *ClientMap,
  OUT UINT8  *ClientActiveCount
  )
{
  EFI_STATUS                   Status;
  HBM_HOST_ENUMERATION_BUFFER  Command;
  UINTN                        Index;
  UINTN                        Index2;
  UINT8                        *ValidAddressesPtr;
  UINT32                       ValidAddresses;

  *ClientActiveCount = 0;

  Status = HeciLocateProtocol ();
  if (EFI_ERROR (Status)) {
    return Status;
  }

  OC_STATIC_ASSERT (sizeof (Command.Request)  == 4, "Invalid ME command size");
  OC_STATIC_ASSERT (sizeof (Command.Response) == 36, "Invalid ME command size");

  ZeroMem (&Command, sizeof (Command));
  Command.Request.Command.Fields.Command = HOST_ENUMERATION_REQUEST;

  Status = HeciSendMessageWithResponse (
    &Command,
    sizeof (Command.Request),
    sizeof (Command.Response)
    );

  if (EFI_ERROR (Status)) {
    return Status;
  }

  ValidAddressesPtr = &Command.Response.ValidAddresses[0];
  for (Index = 0; Index < HBM_ME_CLIENT_MAX; Index += OC_CHAR_BIT) {
    ValidAddresses = *ValidAddressesPtr;

    for (Index2 = 0; Index2 < OC_CHAR_BIT; Index2++) {
      if ((ValidAddresses & (1U << Index2)) != 0) {
        ClientMap[*ClientActiveCount] = (UINT8) (Index + Index2);
        ++(*ClientActiveCount);
      }
    }

    ++ValidAddressesPtr;
  }

  return Status;
}

// Note: Ready
STATIC
EFI_STATUS
HeciGetClientProperties (
  IN  UINT8                   Address,
  OUT HECI_CLIENT_PROPERTIES  *Properties
  )
{
  EFI_STATUS                         Status;
  HBM_HOST_CLIENT_PROPERTIES_BUFFER  Command;

  Status = HeciLocateProtocol ();
  if (EFI_ERROR (Status)) {
    return Status;
  }

  OC_STATIC_ASSERT (sizeof (Command.Request)  == 4, "Invalid ME command size");
  OC_STATIC_ASSERT (sizeof (Command.Response) == 28, "Invalid ME command size");

  ZeroMem (&Command, sizeof (Command));
  Command.Request.Command.Fields.Command = HOST_CLIENT_PROPERTIES_REQUEST;
  Command.Request.Address                = Address;

  Status = HeciSendMessageWithResponse (
    &Command,
    sizeof (Command.Request),
    sizeof (Command.Response)
    );

  CopyMem (
    Properties,
    &Command.Response.ClientProperties,
    sizeof (*Properties)
    );

  return Status;
}

// Note: ready
STATIC
EFI_STATUS
HeciConnectToClient (
  IN UINT8  Address
  )
{
  EFI_STATUS                 Status;
  HBM_CLIENT_CONNECT_BUFFER  Command;

  Status = HeciLocateProtocol ();
  if (EFI_ERROR (Status)) {
    return Status;
  }

  ZeroMem (&Command, sizeof (Command));
  OC_STATIC_ASSERT (sizeof (Command.Request) == 4, "Invalid ME command size");

  Command.Request.Command.Fields.Command = CLIENT_CONNECT_REQUEST;
  Command.Request.MeAddress              = Address;
  Command.Request.HostAddress            = HBM_CLIENT_ADDRESS;

  Status = HeciSendMessageWithResponse (
    &Command,
    sizeof (Command.Request),
    sizeof (Command.Response)
    );

  DEBUG ((DEBUG_INFO, "OC: Connect to client %X code %d - %r\n", Address, Command.Response.Status, Status));

  if (EFI_ERROR (Status)) {
    return Status;
  }

  switch (Command.Response.Status) {
    case HBM_CLIENT_CONNECT_NOT_FOUND:
      return EFI_NOT_FOUND;
    case HBM_CLIENT_CONNECT_ALREADY_CONNECTED:
      return EFI_ALREADY_STARTED;
    case HBM_CLIENT_CONNECT_OUT_OF_RESOURCES:
      return EFI_OUT_OF_RESOURCES;
    case HBM_CLIENT_CONNECT_INVALID_PARAMETER:
      return EFI_INVALID_PARAMETER;
    default:
      mSendingHeciCommandPerClient        = TRUE;
      mCurrentMeClientRequestedReceiveMsg = 0;
      mCurrentMeClientCanReceiveMsg       = 0;
      mCurrentMeClientAddress             = Address;
      return EFI_SUCCESS;
  }
}

// Note: ready
STATIC
EFI_STATUS
HeciSendMessagePerClient (
  IN VOID    *Message,
  IN UINT32  Size
  )
{
  EFI_STATUS  Status;

  Status = EFI_SUCCESS;

  if (mSendingHeciCommandPerClient)  {
    if (!mCurrentMeClientCanReceiveMsg) {
      HeciUpdateReceiveMsgStatus();
    }

    Status = mHeci->SendMsg (
      Message,
      Size,
      HBM_CLIENT_ADDRESS,
      mCurrentMeClientAddress
      );

    if (!EFI_ERROR (Status)) {
      --mCurrentMeClientCanReceiveMsg;
    }
  }

  return Status;
}

// Note: ready
STATIC
EFI_STATUS
HeciPavpRequestProvisioning (
  OUT UINT32  *EpidStatus,
  OUT UINT32  *EpidGroupId
  )
{
  EFI_STATUS                        Status;
  ME_PAVP_PROVISION_REQUEST_BUFFER  Command;

  OC_STATIC_ASSERT (sizeof (Command.Request) == 16, "Invalid ME command size");
  OC_STATIC_ASSERT (sizeof (Command.Response) == 24, "Invalid ME command size");

  ZeroMem (&Command, sizeof (Command));
  Command.Request.Header.Version = ME_PAVP_PROTOCOL_VERSION;
  Command.Request.Header.Command = ME_PAVP_PROVISION_REQUEST_COMMAND;
  HeciSendMessagePerClient (&Command, sizeof (Command.Request));

  ZeroMem (&Command, sizeof (Command));
  Status = HeciGetResponse (&Command, sizeof (Command.Response));

  if (!EFI_ERROR (Status)) {
    *EpidStatus  = Command.Response.Status;
    *EpidGroupId = Command.Response.GroupId;
  }

  return Status;
}

STATIC
VOID
SetProvisioningVariable (
  IN CHAR16  *Variable,
  IN UINT32  Value
  )
{
#ifdef FORCE_PROVISIONING
  (VOID) Variable;
  (VOID) Value;
#else
  gRT->SetVariable (
    Variable,
    &gEfiGlobalVariableGuid,
    EFI_VARIABLE_BOOTSERVICE_ACCESS | EFI_VARIABLE_RUNTIME_ACCESS | EFI_VARIABLE_NON_VOLATILE,
    sizeof (Value),
    &Value
    );
#endif
}

// Note: ready
STATIC
EFI_STATUS
GetGroupPublicKey (
  IN  EPID_GROUP_PUBLIC_KEY  *PublicKeys,
  IN  UINTN                  PublicKeyCount,
  IN  UINT32                 Key,
  OUT EPID_GROUP_PUBLIC_KEY  **ChosenPublicKey
  )
{
  EFI_STATUS  Status;
  UINT32      Index;

  Status = EFI_NOT_FOUND;

  for (Index = 0; Index < PublicKeyCount; ++Index) {
    if (SwapBytes32 (PublicKeys[Index].GroupId) == Key) {
      *ChosenPublicKey = &PublicKeys[Index];
      return EFI_SUCCESS;
    }
  }

  return Status;
}

// Note: ready
STATIC
EFI_STATUS
HeciPavpPerformProvisioning (
  IN EPID_CERTIFICATE       *EpidCertificate,
  IN EPID_GROUP_PUBLIC_KEY  *EpidGroupPublicKey
  )
{
  EFI_STATUS                        Status;
  ME_PAVP_PROVISION_PERFORM_BUFFER  Command;
  UINTN                             Index;

  OC_STATIC_ASSERT (sizeof (Command.Request) == 1284, "Invalid ME command size");
  OC_STATIC_ASSERT (sizeof (Command.Response) == 16, "Invalid ME command size");


  ZeroMem (&Command, sizeof (Command));
  Command.Request.Header.Version     = ME_PAVP_PROTOCOL_VERSION;
  Command.Request.Header.Command     = ME_PAVP_PROVISION_PERFORM_COMMAND;
  Command.Request.Header.PayloadSize = ME_PAVP_PROVISION_PERFORM_PAYLOAD_SIZE;
  CopyMem (&Command.Request.Certificate, EpidCertificate, sizeof (Command.Request.Certificate));
  CopyMem (&Command.Request.PublicKey, EpidGroupPublicKey, sizeof (Command.Request.PublicKey));

  Status = HeciSendMessagePerClient (&Command, sizeof (Command.Request));
  ZeroMem (&Command, sizeof (Command));
  if (EFI_ERROR (Status)) {
    DEBUG ((DEBUG_INFO, "OC: Failed to send provisioning command - %r\n", Status));
    return EFI_DEVICE_ERROR;
  }

  for (Index = 0; Index < 3; ++Index) {
    Status = HeciGetResponse (&Command, sizeof (Command.Response));
    if (Status != EFI_TIMEOUT) {
      break;
    }
  }

  DEBUG ((
    DEBUG_INFO,
    "OC: Finished provisioning command with status %x - %r\n",
    Command.Response.Header.Status,
    Status
    ));

  if (EFI_ERROR (Status)) {
    return EFI_DEVICE_ERROR;
  }

  if (Command.Response.Header.Status != EPID_STATUS_PROVISIONED) {
    Status = EFI_DEVICE_ERROR;
  }

  if (Command.Response.Header.Status == EPID_STATUS_FAIL_PROVISION) {
    SetProvisioningVariable (APPLE_EPID_PROVISIONED_VARIABLE_NAME, 1);
  }

  return Status;
}

STATIC
EFI_STATUS
HeciFpfGetStatus (
  OUT  UINT32 *FpfStatus
  )
{
  EFI_STATUS  Status;
  UINT32      Response[11];
  UINT32      Request[4];

  ZeroMem (Request, sizeof (Request));
  Request[0] = 3;
  HeciSendMessagePerClient (Request, sizeof (Request));

  ZeroMem (Response, sizeof (Response));
  Status = HeciGetResponse (Response, sizeof (Response));

  if (!EFI_ERROR (Status)) {
    *FpfStatus = Response[1];
  }

  return Status;
}

STATIC
EFI_STATUS
HeciFpfProvision (
  OUT  UINT32 *FpfStatus
  )
{
  EFI_STATUS Status;
  UINT32     Response[2];
  UINT32     Request[3];

  ZeroMem (Request, sizeof (Request));
  Request[0] = 5;
  Request[1] = 1;
  Request[2] = 255;
  HeciSendMessagePerClient (Request, sizeof (Request));

  ZeroMem (Response, sizeof (Response));
  Status = HeciGetResponse (Response, sizeof (Response));

  if (!EFI_ERROR (Status)) {
    *FpfStatus = Response[1];
  }

  return Status;
}

// Note: ready
STATIC
EFI_STATUS
HeciDisconnectFromClients (
  VOID
  )
{
  EFI_STATUS                    Status;
  HBM_CLIENT_DISCONNECT_BUFFER  Command;

  Status = EFI_SUCCESS;

  if (mSendingHeciCommandPerClient) {
    OC_STATIC_ASSERT (sizeof (Command) == 4, "Invalid ME command size");

    ZeroMem (&Command, sizeof (Command));

    Command.Request.Command.Fields.Command = CLIENT_DISCONNECT_REQUEST;
    Command.Request.MeAddress              = mCurrentMeClientAddress;
    Command.Request.HostAddress            = HBM_CLIENT_ADDRESS;

    ++mCurrentMeClientRequestedReceiveMsg;

    Status = HeciSendMessageWithResponse (
      &Command,
      sizeof (Command.Request),
      sizeof (Command.Response)
      );

    DEBUG ((
      DEBUG_INFO,
      "OC: Disconnect from client %X code %d - %r\n",
      mCurrentMeClientAddress,
      Command.Response.Status,
      Status
      ));

    if (!EFI_ERROR (Status)) {
      mSendingHeciCommandPerClient = FALSE;
    }
  }

  return Status;
}

// Note: ready
BOOLEAN
IsBuiltinGpuAvailable (
  VOID
  )
{
  EFI_STATUS                       Status;
  UINT32                           Value;
  EFI_PCI_ROOT_BRIDGE_IO_PROTOCOL  *Interface;

  Status = gBS->LocateProtocol (
    &gEfiPciRootBridgeIoProtocolGuid,
    NULL,
    (VOID **) &Interface
    );

  if (EFI_ERROR (Status)) {
    DEBUG ((DEBUG_INFO, "OC: Failed to find PCI root protocol - %r\n", Status));
    return FALSE;
  }

  //
  // IGPU_DEVICE_ID       = 0x2
  // PCI_VENDOR_ID_OFFSET = 0x0
  // (IGPU_DEVICE_ID << 16U | PCI_VENDOR_ID_OFFSET)
  // See EFI_PCI_ADDRESS
  //
  Status = Interface->Pci.Read (
    Interface,
    EfiPciWidthUint32,
    0x20000,
    1,
    &Value
    );

  if (EFI_ERROR (Status)) {
    DEBUG ((DEBUG_INFO, "OC: Failed to read from IGPU device - %r\n", Status));
    return FALSE;
  }

  DEBUG ((DEBUG_INFO, "OC: IGPU is %X\n", Value));

  return Value != 0xFFFFFFFFU;
}

// Note: Ready
STATIC
EFI_STATUS
NeedsEpidProvisioning (
  VOID
  )
{
  EFI_STATUS  Status;
  UINT32      Data;
  UINTN       DataSize;

  if (IsBuiltinGpuAvailable()) {
    DataSize = sizeof (Data);

    Status = gRT->GetVariable (
      APPLE_EPID_PROVISIONED_VARIABLE_NAME,
      &gEfiGlobalVariableGuid,
      NULL,
      &DataSize,
      &Data
      );

#ifdef FORCE_PROVISIONING
    Data = 0;
#endif

    if (EFI_ERROR (Status) || Data != 1) {
      return EFI_SUCCESS;
    }
  }

  return EFI_NOT_FOUND;
}

// Note: ready
STATIC
EFI_STATUS
NeedsFpfProvisioning (
  VOID
  )
{
  EFI_STATUS                   Status;
  UINT32                       Data;
  UINTN                        DataSize;
  APPLE_FPF_CONFIGURATION_HOB  *Hob;

#if 0
  Hob = GetFirstGuidHob (&gAppleFpfConfigurationHobGuid);
#else
  Hob = NULL;
#endif

  DEBUG ((DEBUG_INFO, "OC: HOB for FPF is %p\n", Hob));

  if (Hob == NULL || Hob->ShouldProvision) {
    DataSize = sizeof (Data);
    Status = gRT->GetVariable (
      APPLE_FPF_PROVISIONED_VARIABLE_NAME,
      &gEfiGlobalVariableGuid,
      NULL,
      &DataSize,
      &Data
      );

    if (EFI_ERROR (Status) || Data != 1) {
      return EFI_SUCCESS;
    }

    return EFI_NOT_FOUND;
  }

  return EFI_UNSUPPORTED;
}

EFI_STATUS
OcPerformEpidProvisioning (
  VOID
  )
{
  EFI_STATUS              Status;
  UINTN                   Index;
  HECI_CLIENT_PROPERTIES  Properties;
  EPID_GROUP_PUBLIC_KEY   *EpidGroupPublicKeys;
  EPID_CERTIFICATE        *EpidCertificate;
  UINT32                  EpidGroupPublicKeysCount;
  UINT32                  EpidStatus;
  UINT32                  EpidGroupId;
  EPID_GROUP_PUBLIC_KEY   *EpidCurrentGroupPublicKey;

  Status = NeedsEpidProvisioning ();
  DEBUG ((DEBUG_INFO, "OC: Needs provisioning EPID - %r\n", Status));
  if (EFI_ERROR (Status)) {
    return EFI_ALREADY_STARTED;
  }

  Status = HeciLocateProtocol ();
  DEBUG ((DEBUG_INFO, "OC: HECI protocol lookup - %r\n", Status));
  if (EFI_ERROR (Status)) {
    return Status;
  }

  Status = ReadProvisioningData (
    &EpidCertificate,
    &EpidGroupPublicKeys,
    &EpidGroupPublicKeysCount
    );
  DEBUG ((DEBUG_INFO, "OC: Provisioning data - %r\n", Status));
  if (EFI_ERROR (Status)) {
    return Status;
  }

  //
  // Get Management Engine proccesses namespace.
  // Each App like PAVP or FPF have unique identifier represented as GUID.
  //
  Status = HeciGetClientMap (mMeClientMap, &mMeClientActiveCount);
  DEBUG ((DEBUG_INFO, "OC: Got %d clients - %r\n", mMeClientActiveCount, Status));
  if (EFI_ERROR (Status)) {
    return Status;
  }

  Status = EFI_NOT_FOUND;

  for (Index = 0; Index < mMeClientActiveCount; ++Index) {
    Status = HeciGetClientProperties (
      mMeClientMap[Index],
      &Properties
      );

    DEBUG ((
      DEBUG_INFO,
      "OC: Client %u has %g protocol - %r\n",
      (UINT32) Index,
      Properties.ProtocolName,
      Status
      ));

    if (EFI_ERROR (Status)) {
      break;
    }

    if (CompareGuid (&Properties.ProtocolName, &gMePavpProtocolGuid)) {
      break;
    }
  }

  if (!EFI_ERROR (Status) && Index != mMeClientActiveCount) {
    DEBUG ((DEBUG_INFO, "OC: Found application at %u\n", (UINT32) Index));

    Status = HeciConnectToClient (mMeClientMap[Index]);
    if (!EFI_ERROR (Status)) {

      EpidStatus = EpidGroupId = 0;
      Status = HeciPavpRequestProvisioning (&EpidStatus, &EpidGroupId);

      DEBUG ((DEBUG_INFO, "OC: Got EPID status %X and group id %x - %r\n", EpidStatus, EpidGroupId, Status));
    }

    if (!EFI_ERROR (Status)) {

      if (EpidStatus == EPID_STATUS_PROVISIONED) {
        SetProvisioningVariable (APPLE_EPID_PROVISIONED_VARIABLE_NAME, 1);
      } else if (EpidStatus == EPID_STATUS_CAN_PROVISION) {
        Status = GetGroupPublicKey (
          EpidGroupPublicKeys,
          EpidGroupPublicKeysCount,
          EpidGroupId,
          &EpidCurrentGroupPublicKey
          );

        DEBUG ((DEBUG_INFO, "OC: Got EPID group public key - %r\n", Status));

        if (!EFI_ERROR (Status)) {
          Status = HeciPavpPerformProvisioning (EpidCertificate, EpidCurrentGroupPublicKey);
          DEBUG ((DEBUG_INFO, "OC: Sent EPID certificate - %r\n", Status));
          if (!EFI_ERROR (Status)) {
            SetProvisioningVariable (APPLE_EPID_PROVISIONED_VARIABLE_NAME, 1);
          }
        }
      }
    }

    HeciDisconnectFromClients ();

  } else {
    DEBUG ((DEBUG_INFO, "OC: No EPID application found\n"));

    if (Index == mMeClientActiveCount) {
      //
      // Do not retry provisioning on incompatible firmware.
      // TODO: Do we really need this?
      //
      SetProvisioningVariable (APPLE_EPID_PROVISIONED_VARIABLE_NAME, 1);
      Status = EFI_NOT_FOUND;
    }
  }

  gBS->FreePool (EpidCertificate);
  gBS->FreePool (EpidGroupPublicKeys);

  return Status;
}

EFI_STATUS
OcPerformFpfProvisioning (
  VOID
  )
{
  EFI_STATUS              Status;
  UINTN                   Index;
  HECI_CLIENT_PROPERTIES  Properties;
  UINT32                  FpfStatus;

  Status = NeedsFpfProvisioning ();
  DEBUG ((DEBUG_INFO, "OC: Needs provisioning FPF - %r\n", Status));
  if (EFI_ERROR (Status)) {
    return EFI_ALREADY_STARTED;
  }

  Status = HeciLocateProtocol ();
  DEBUG ((DEBUG_INFO, "OC: HECI protocol lookup - %r\n", Status));
  if (EFI_ERROR (Status)) {
    return Status;
  }

  //
  // Get Management Engine proccesses namespace.
  // Each App like PAVP or FPF have unique identifier represented as GUID.
  //
  Status = HeciGetClientMap (mMeClientMap, &mMeClientActiveCount);
  DEBUG ((DEBUG_INFO, "OC: Got %d clients - %r\n", mMeClientActiveCount, Status));
  if (EFI_ERROR (Status)) {
    return Status;
  }

  Status = EFI_NOT_FOUND;

  for (Index = 0; Index < mMeClientActiveCount; ++Index) {
    Status = HeciGetClientProperties (
      mMeClientMap[Index],
      &Properties
      );

    DEBUG ((
      DEBUG_INFO,
      "OC: Client %u has %g protocol - %r\n",
      (UINT32) Index,
      Properties.ProtocolName,
      Status
      ));

    if (EFI_ERROR (Status)) {
      break;
    }

    if (CompareGuid (&Properties.ProtocolName, &gMeFpfProtocolGuid)) {
      break;
    }
  }

  if (!EFI_ERROR (Status) && Index != mMeClientActiveCount) {
    DEBUG ((DEBUG_INFO, "OC: Found application at %u\n", (UINT32) Index));

    Status = HeciConnectToClient (mMeClientMap[Index]);

    //
    // I *think* FPF provisioning locks fuses from further update.
    // For this reason we do not want it.
    //
    if (!EFI_ERROR (Status)) {
      Status = HeciFpfGetStatus (&FpfStatus);
      DEBUG ((DEBUG_INFO, "OC: Got FPF status %u - %r\n", FpfStatus, Status));
      if (!EFI_ERROR (Status)) {
        if (FpfStatus == 250) {
          Status = HeciFpfProvision (&FpfStatus);
          DEBUG ((DEBUG_INFO, "OC: Got FPF provisioning %u - %r\n", FpfStatus, Status));
          if (!EFI_ERROR (Status) && FpfStatus == 0) {
            SetProvisioningVariable (APPLE_FPF_PROVISIONED_VARIABLE_NAME, 1);
          } else {
            Status = EFI_DEVICE_ERROR;
          }
        } else {
          Status = EFI_DEVICE_ERROR;
        }
      }

      HeciDisconnectFromClients ();
    }
  } else {
    DEBUG ((DEBUG_INFO, "OC: No FPF application found\n"));

    if (Index == mMeClientActiveCount) {
      //
      // Do not retry provisioning on incompatible firmware.
      // TODO: Do we really need this?
      //
      SetProvisioningVariable (APPLE_FPF_PROVISIONED_VARIABLE_NAME, 1);
      Status = EFI_NOT_FOUND;
    }
  }

  return Status;
}


VOID
OcPerformProvisioning (
  VOID
  )
{
  EFI_STATUS  Status;

  DEBUG ((DEBUG_INFO, "OC: Starting EPID provisioning\n"));

  Status = OcPerformEpidProvisioning ();

  DEBUG ((DEBUG_INFO, "OC: Done EPID provisioning - %r\n", Status));

#if 0
  DEBUG ((DEBUG_INFO, "OC: Starting FPF provisioning\n"));

  Status = OcPerformFpfProvisioning ();

  DEBUG ((DEBUG_INFO, "OC: Done FPF provisioning - %r\n", Status));
#endif
}
