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

#include <Security/SecAccess.h>
#include <security_keychain/Access.h>
#include "SecBridge.h"


//
// CF boilerplate
//
CFTypeID SecAccessGetTypeID(void)
{
	BEGIN_SECAPI
	return gTypes().Access.typeID;
	END_SECAPI1(_kCFRuntimeNotATypeID)
}


//
// API bridge calls
//
/*!
 *	Create a new SecAccessRef that is set to the default configuration
 *	of a (newly created) security object.
 */
OSStatus SecAccessCreate(CFStringRef descriptor, CFArrayRef trustedList, SecAccessRef *accessRef)
{
	BEGIN_SECAPI
	Required(descriptor);
	SecPointer<Access> access;
	if (trustedList) {
		CFIndex length = CFArrayGetCount(trustedList);
		ACL::ApplicationList trusted;
		for (CFIndex n = 0; n < length; n++)
			trusted.push_back(TrustedApplication::required(
				SecTrustedApplicationRef(CFArrayGetValueAtIndex(trustedList, n))));
		access = new Access(cfString(descriptor), trusted);
	} else {
		access = new Access(cfString(descriptor));
	}
	Required(accessRef) = access->handle();
	END_SECAPI
}


/*!
 */
OSStatus SecAccessCreateFromOwnerAndACL(const CSSM_ACL_OWNER_PROTOTYPE *owner,
	uint32 aclCount, const CSSM_ACL_ENTRY_INFO *acls,
	SecAccessRef *accessRef)
{
	BEGIN_SECAPI
	Required(accessRef);	// preflight
	SecPointer<Access> access = new Access(Required(owner), aclCount, &Required(acls));
	*accessRef = access->handle();
	END_SECAPI
}


/*!
 */
OSStatus SecAccessGetOwnerAndACL(SecAccessRef accessRef,
	CSSM_ACL_OWNER_PROTOTYPE_PTR *owner,
	uint32 *aclCount, CSSM_ACL_ENTRY_INFO_PTR *acls)
{
	BEGIN_SECAPI
	Access::required(accessRef)->copyOwnerAndAcl(
		Required(owner), Required(aclCount), Required(acls));
	END_SECAPI
}


/*!
 */
OSStatus SecAccessCopyACLList(SecAccessRef accessRef,
	CFArrayRef *aclList)
{
	BEGIN_SECAPI
	Required(aclList) = Access::required(accessRef)->copySecACLs();
	END_SECAPI
}


/*!
 */
OSStatus SecAccessCopySelectedACLList(SecAccessRef accessRef,
	CSSM_ACL_AUTHORIZATION_TAG action,
	CFArrayRef *aclList)
{
	BEGIN_SECAPI
	Required(aclList) = Access::required(accessRef)->copySecACLs(action);
	END_SECAPI
}