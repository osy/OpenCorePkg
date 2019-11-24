/** @file
  Copyright (C) 2019, vit9696. All rights reserved.

  All rights reserved.

  This program and the accompanying materials
  are licensed and made available under the terms and conditions of the BSD License
  which accompanies this distribution.  The full text of the license may be found at
  http://opensource.org/licenses/bsd-license.php

  THE PROGRAM IS DISTRIBUTED UNDER THE BSD LICENSE ON AN "AS IS" BASIS,
  WITHOUT WARRANTIES OR REPRESENTATIONS OF ANY KIND, EITHER EXPRESS OR IMPLIED.
**/

#ifndef APPLE_PROVISIONING_H
#define APPLE_PROVISIONING_H

#define APPLE_EPID_CERTIFICATE_FILE_GUID \
  { 0xD1A26C1F, 0xABF5, 0x4806, { 0xBB, 0x24, 0x68, 0xD3, 0x17, 0xE0, 0x71, 0xD5 } }

#define APPLE_EPID_GROUP_PUBLIC_KEYS_FILE_GUID \
  { 0x2906CC1F, 0x09CA, 0x4457, { 0x9A, 0x4F, 0xC2, 0x12, 0xC5, 0x45, 0xD3, 0xD3 } }

extern EFI_GUID gAppleEpidCertificateFileGuid;
extern EFI_GUID gAppleEpidGroupPublicKeysFileGuid;

extern UINT8 gDefaultAppleEpidCertificate[];
extern UINTN gDefaultAppleEpidCertificateSize;

extern UINT8 gDefaultAppleGroupPublicKeys[];
extern UINTN gDefaultAppleGroupPublicKeysSize;

#define APPLE_EPID_PROVISIONED_VARIABLE_NAME L"epid_provisioned"

#endif // APPLE_PROVISIONING_H
