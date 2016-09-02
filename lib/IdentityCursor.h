/*
 * Copyright (c) 2002-2004 Apple Computer, Inc. All Rights Reserved.
 * 
 * @APPLE_LICENSE_HEADER_START@
 * 
 * Copyright (c) 1999-2003 Apple Computer, Inc.  All Rights Reserved.
 * 
 * This file contains Original Code and/or Modifications of Original Code
 * as defined in and that are subject to the Apple Public Source License
 * Version 2.0 (the 'License'). You may not use this file except in
 * compliance with the License. Please obtain a copy of the License at
 * http://www.opensource.apple.com/apsl/ and read it before using this
 * file.
 * 
 * The Original Code and all software distributed under the License are
 * distributed on an 'AS IS' basis, WITHOUT WARRANTY OF ANY KIND, EITHER
 * EXPRESS OR IMPLIED, AND APPLE HEREBY DISCLAIMS ALL SUCH WARRANTIES,
 * INCLUDING WITHOUT LIMITATION, ANY WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE, QUIET ENJOYMENT OR NON-INFRINGEMENT.
 * Please see the License for the specific language governing rights and
 * limitations under the License.
 * 
 * @APPLE_LICENSE_HEADER_END@
 */

//
// IdentityCursor.h - Working with IdentityCursors
//
#ifndef _SECURITY_IDENTITYCURSOR_H_
#define _SECURITY_IDENTITYCURSOR_H_

#include <Security/SecCertificate.h>
#include <Security/SecIdentity.h>
#include <Security/SecIdentitySearch.h>
#include <security_cdsa_client/securestorage.h>
#include <security_keychain/KCCursor.h>
#include <CoreFoundation/CFArray.h>

namespace Security
{

namespace KeychainCore
{

class Identity;
class KeyItem;

class IdentityCursor : public SecCFObject
{
    NOCOPY(IdentityCursor)
public:
	SECCFFUNCTIONS(IdentityCursor, SecIdentitySearchRef, errSecInvalidSearchRef, gTypes().IdentityCursor)

    IdentityCursor(const StorageManager::KeychainList &searchList, CSSM_KEYUSE keyUsage);
	virtual ~IdentityCursor() throw();
	virtual bool next(SecPointer<Identity> &identity);

private:
	StorageManager::KeychainList mSearchList;
	KCCursor mKeyCursor;
	KCCursor mCertificateCursor;
	SecPointer<KeyItem> mCurrentKey;
};

class IdentityCursorPolicyAndID : public IdentityCursor
{
public:
    IdentityCursorPolicyAndID(const StorageManager::KeychainList &searchList, CSSM_KEYUSE keyUsage, CFStringRef idString, SecPolicyRef policy, bool returnOnlyValidIdentities);
	virtual bool next(SecPointer<Identity> &identity);
private:
	SecPolicyRef mPolicy;//%%%Use C++policy object
	CFStringRef mIDString;
	bool mReturnOnlyValidIdentities;
};


} // end namespace KeychainCore

} // end namespace Security

#endif // !_SECURITY_IDENTITYCURSOR_H_