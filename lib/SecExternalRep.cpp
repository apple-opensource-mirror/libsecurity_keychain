/*
 * Copyright (c) 2004 Apple Computer, Inc. All Rights Reserved.
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
 *
 * SecExternalRep.cpp - private class representing an external representation of
 *					    a SecKeychainItemRef, used by SecImportExport.h
 */

#include "SecExternalRep.h"
#include "SecImportExportPem.h"
#include "SecImportExportAgg.h"
#include "SecImportExportUtils.h"
#include "SecImportExportPkcs8.h"
#include "SecImportExportCrypto.h"
#include <security_utilities/errors.h>
#include <Security/SecKeyPriv.h>
#include <Security/SecCertificate.h>
#include <Security/cssmapi.h>
#include <CoreServices.framework/Frameworks/CarbonCore.framework/Headers/MacErrors.h>

using namespace Security;
using namespace KeychainCore;


#pragma mark --- SecExportRep Subclasses seen only by SecExportRep::vend() ---

namespace SecExport {

class Key : public SecExportRep 
{
	friend class SecExportRep;
protected:
	Key(
		CFTypeRef						kcItemRef);
	~Key();
	OSStatus exportRep(
		SecExternalFormat					format,	
		SecItemImportExportFlags			flags,	
		const SecKeyImportExportParameters	*keyParams,		// optional 
		CFMutableDataRef					outData,		// data appended here
		const char							**pemHeader);   // e.g., "RSA PUBLIC KEY"
		
private:
	CSSM_ALGORITHMS						mKeyAlg;	
	const CSSM_KEY						*mCssmKey;	
};

class Cert : public SecExportRep 
{
	friend class SecExportRep;
protected:
	Cert(
		CFTypeRef						kcItemRef);
	~Cert();
	OSStatus exportRep(
		SecExternalFormat					format,	
		SecItemImportExportFlags			flags,	
		const SecKeyImportExportParameters	*keyParams,		// optional 
		CFMutableDataRef					outData,		// data appended here
		const char							**pemHeader);   // e.g., "RSA PUBLIC KEY"
};

}   /* namespace SecExport */

#pragma mark --- SecExportRep: Representation of an internal object on export ---

SecExportRep::SecExportRep(
	CFTypeRef			kcItemRef) :
		mKcItem((SecKeychainItemRef)kcItemRef),
		mPemParamLines(NULL)
{
	CFRetain(mKcItem);
}

SecExportRep::~SecExportRep()
{
	if(mKcItem) {
		CFRelease(mKcItem);
	}
	if(mPemParamLines) {
		CFRelease(mPemParamLines);
	}
}

SecExportRep::SecExportRep() {
	MacOSError::throwMe(errSecInvalidItemRef);
}

/* must be implemented by subclass */
OSStatus SecExportRep::exportRep(
	SecExternalFormat					format,	
	SecItemImportExportFlags			flags,	
	const SecKeyImportExportParameters	*keyParams,		// optional 
	CFMutableDataRef					outData,		// data appended here
	const char							**pemHeader)	// e.g., "X509 CERTIFICATE"
{
	MacOSError::throwMe(errSecInvalidItemRef);
}

/*
 * Sole public means of obtaining a SecExportRep object. In fact only instances
 * of subclasses are vended but caller does not know that.
 * 
 * Gleans SecExternalItemType from incoming type, throws MacOSError if
 * incoming type is bogus.
 *
 * Vended object holds a reference to kcItem for its lifetime. 
 */
SecExportRep *SecExportRep::vend(	
	CFTypeRef						kcItemRef)
{
	CFTypeID itemType = CFGetTypeID(kcItemRef);
	if(itemType == SecCertificateGetTypeID()) {
		return new SecExport::Cert(kcItemRef);
	}
	else if(itemType == SecKeyGetTypeID()) {
		return new SecExport::Key(kcItemRef);
	}
	else {
		MacOSError::throwMe(errSecInvalidItemRef);
	}
}

#pragma mark --- Key External rep ---

SecExport::Key::Key(
	CFTypeRef kcItemRef) :
		SecExportRep(kcItemRef)
{
	
	/* figure out if it's public, private, or session */
	OSStatus ortn;
	ortn = SecKeyGetCSSMKey((SecKeyRef)kcItemRef, &mCssmKey);
	if(ortn) {
		SecImpExpDbg("SecKeyGetCSSMKey failure in SecExportRep::Key()");
		MacOSError::throwMe(ortn);
	}
	switch(mCssmKey->KeyHeader.KeyClass) {
		case CSSM_KEYCLASS_PUBLIC_KEY:
			mExternType = kSecItemTypePublicKey;
			SecImpExpDbg("SecExportRep::Key(): SET_PubKey");
			break;
		case CSSM_KEYCLASS_PRIVATE_KEY:
			mExternType = kSecItemTypePrivateKey;
			SecImpExpDbg("SecExportRep::Key(): SET_PrivKey");
			break;
		case CSSM_KEYCLASS_SESSION_KEY:
			mExternType = kSecItemTypeSessionKey;
			SecImpExpDbg("SecExportRep::Key(): SET_SessionKey");
			break;
		default:
			SecImpExpDbg("SecExportRep::Key(): invalid KeyClass (%lu)",  
				mCssmKey->KeyHeader.KeyClass);
			MacOSError::throwMe(errSecInvalidItemRef);
	}
	mKeyAlg = mCssmKey->KeyHeader.AlgorithmId;
}

SecExport::Key::~Key()
{
	/* nothing for now */
}

/* 
 * The heart of this class: cook up external representation, appending to 
 * existing CFMutableDataRef.
 */
OSStatus SecExport::Key::exportRep(
	SecExternalFormat					format,	
	SecItemImportExportFlags			flags,			
	const SecKeyImportExportParameters	*keyParams,	// optional 
	CFMutableDataRef					outData,	// data appended here
	const char							**pemHeader)// e.g., "X509 CERTIFICATE"
{
	assert(outData != NULL);
	assert(mKcItem != NULL);
	assert(mCssmKey != NULL);
		
	/* 
	 * Reject unsupported formats here.
	 */
	switch(format) {
		case kSecFormatWrappedPKCS8:
			return impExpPkcs8Export((SecKeyRef)mKcItem, flags, keyParams,
				outData, pemHeader);
		case kSecFormatWrappedOpenSSL:
			return impExpWrappedKeyOpenSslExport((SecKeyRef)mKcItem, flags, keyParams,
				outData, pemHeader, &mPemParamLines);
		case kSecFormatSSH:
		case kSecFormatWrappedSSH:
		case kSecFormatWrappedLSH:
			return errSecUnsupportedFormat;
		default:
			break;
	}
	
	/* 
	 * Remaining formats just do a NULL key wrap. Figure out the appropriate
	 * CDSA_specific format and wrap parameters. 
	 */
	OSStatus ortn = noErr;
	CSSM_KEYBLOB_FORMAT blobForm;
	
	switch(mExternType) {
		case kSecItemTypePublicKey:
			switch(mKeyAlg) {
				case CSSM_ALGID_RSA:
					*pemHeader = PEM_STRING_RSA_PUBLIC;
					break;  
				case CSSM_ALGID_DH:
					*pemHeader = PEM_STRING_DH_PUBLIC;
					break;  
				case CSSM_ALGID_DSA:
					*pemHeader = PEM_STRING_DSA_PUBLIC;
					break; 
				default:
					SecImpExpDbg("SecExportRep::exportRep unknown public key alg %lu",
						mKeyAlg);
					return errSecUnsupportedFormat;
			}		/* end switch(mKeyAlg) */
			break;  /* from case externType kSecItemTypePublicKey */
			
		case kSecItemTypePrivateKey:
			switch(mKeyAlg) {
				case CSSM_ALGID_RSA:
					*pemHeader = PEM_STRING_RSA;
					break; 
				case CSSM_ALGID_DH:
					*pemHeader = PEM_STRING_DH_PRIVATE;
					break; 
				case CSSM_ALGID_DSA:
					*pemHeader = PEM_STRING_DSA;
					break;  /* from case RSA private */
				default:
					SecImpExpDbg("SecExportRep::exportRep unknown private key alg "
						"%lu", mKeyAlg);
					return errSecUnsupportedFormat;
			}		/* end switch(mKeyAlg) */
			break;  /* from case externType kSecItemTypePrivateKey */
			
		case kSecItemTypeSessionKey:
			*pemHeader = PEM_STRING_SESSION;
			break; 
		default:
			assert(0);
			return errSecInvalidItemRef;
	}   /* switch(mExternType) */
	
	/* Map our external params to CDSA blob format */
	CSSM_KEYCLASS keyClass;
	ortn = impExpKeyForm(format, mExternType, mKeyAlg, &blobForm, &keyClass);
	if(ortn) {
		return ortn;
	}

	/* Specify format of null-wrapped key */
	CSSM_ATTRIBUTE_TYPE formatAttrType = CSSM_ATTRIBUTE_NONE;
	switch(mExternType) {
		case kSecItemTypePrivateKey:
			formatAttrType = CSSM_ATTRIBUTE_PRIVATE_KEY_FORMAT;
			break;
		case kSecItemTypePublicKey:
			formatAttrType = CSSM_ATTRIBUTE_PUBLIC_KEY_FORMAT;
			break;
		/* symmetric key doesn't have a format */
		default:
			break;
	}
	
	CSSM_CSP_HANDLE cspHand;
	ortn = SecKeyGetCSPHandle((SecKeyRef)mKcItem, &cspHand);
	if(ortn) {
		SecImpExpDbg("SecExportRep::exportRep SecKeyGetCSPHandle error");
		return ortn;
	}

	/* perform the NULL wrap --> wrapped Key */
	CSSM_KEY wrappedKey;
	memset(&wrappedKey, 0, sizeof(wrappedKey));
	ortn = impExpExportKeyCommon(cspHand, 
		(SecKeyRef)mKcItem, 
		NULL,							// wrappingKey not used for NULL
		&wrappedKey,					// destination
		CSSM_ALGID_NONE,				
		CSSM_ALGMODE_NONE, 
		CSSM_PADDING_NONE,
		CSSM_KEYBLOB_WRAPPED_FORMAT_NONE, 
		formatAttrType,
		blobForm,
		NULL);							// IV
		
	if(ortn == CSSM_OK) {
		/* pass key data back to caller */
		CFDataAppendBytes(outData, wrappedKey.KeyData.Data, wrappedKey.KeyData.Length);
	}
	CSSM_FreeKey(cspHand, NULL, &wrappedKey, CSSM_FALSE);
	return ortn;
}

#pragma mark --- Certificate External rep ---

SecExport::Cert::Cert(
	CFTypeRef kcItemRef) :
		SecExportRep(kcItemRef)
{
	mExternType = kSecItemTypeCertificate;
}

SecExport::Cert::~Cert()
{
	/* nothing for now */
}

/* 
 * The heart of this class: cook up external representation, appending to 
 * existing CFMutableDataRef.
 */
OSStatus SecExport::Cert::exportRep(
	SecExternalFormat					format,	
	SecItemImportExportFlags			flags,			
	const SecKeyImportExportParameters	*keyParams,	// optional 
	CFMutableDataRef					outData,	// data appended here
	const char							**pemHeader)// e.g., "X509 CERTIFICATE"
{
	assert(outData != NULL);
	assert(mKcItem != NULL);
		
	switch(format) {
		case kSecFormatUnknown:		// default
		case kSecFormatX509Cert:	// currently, only supported format
			break;
		default:
			SecImpExpDbg("SecExportRep::exportRep unsupported format for cert");
			return errSecUnsupportedFormat;
	}
	
	CSSM_DATA cdata;
	OSStatus ortn = SecCertificateGetData((SecCertificateRef)mKcItem, &cdata);
	if(ortn) {
		SecImpExpDbg("SecExportRep::exportRep SecCertificateGetData error");
		return ortn;
	}
	CFDataAppendBytes(outData, cdata.Data, cdata.Length);
	*pemHeader = PEM_STRING_X509;
	return noErr;
}
	
#pragma mark --- SecImportRep: Representation of an external object on import ---

/* 
 * for import, when we have the external representation.
 * All arguments except for the CFDataRef are optional (i.e., "unknown"
 * is legal).
 */
SecImportRep::SecImportRep(
	CFDataRef						external,
	SecExternalItemType				externType,		// may be unknown 
	SecExternalFormat				externFormat,	// may be unknown
	CSSM_ALGORITHMS					keyAlg,			// may be unknown, CSSM_ALGID_NONE
	CFArrayRef						pemParamLines /* = NULL */ ) :
		mExternal(external),
		mExternType(externType),
		mExternFormat(externFormat),
		mKeyAlg(keyAlg),
		mPemParamLines(pemParamLines)
{
	CFRetain(mExternal);
}

SecImportRep::~SecImportRep()
{
	if(mExternal) {
		CFRelease(mExternal);
	}
	if(mPemParamLines) {
		CFRelease(mPemParamLines);
	}
}

/* 
 * Convert to one or more SecItemRefs and/or import to keychain.
 * The cspHand handle MUST be a CSPDL handle, not a raw CSP handle. 
 */
OSStatus SecImportRep::importRep(
	SecKeychainRef						importKeychain,		// optional
	CSSM_CSP_HANDLE						cspHand,			// required
	SecItemImportExportFlags			flags,
	const SecKeyImportExportParameters	*keyParams,			// optional 
	ImpPrivKeyImportState				&keyImportState,	// IN/OUT
	CFMutableArrayRef					outArray)			// optional, append here 
{
	/* caller must have sorted this out by now */
	assert((mExternType != kSecItemTypeUnknown) &&
		   (mExternFormat != kSecFormatUnknown));
		   
	/* app could conceivably botch these with crafty PEM hacking */
	if((mExternal == NULL) || (CFDataGetLength(mExternal) == 0)) {
		return paramErr;
	}
	
	/* handle the easy ones first */
	switch(mExternFormat) {
		case kSecFormatX509Cert:
		{
			CSSM_DATA cdata;
			cdata.Data = (uint8 *)CFDataGetBytePtr(mExternal);
			cdata.Length = CFDataGetLength(mExternal);
			return impExpImportCertCommon(&cdata, importKeychain, outArray);
		}

		case kSecFormatPKCS12:
			return impExpPkcs12Import(mExternal, flags, keyParams, 
				keyImportState, importKeychain, cspHand, outArray);
		case kSecFormatPKCS7:
			return impExpPkcs7Import(mExternal, flags, keyParams, importKeychain, 
				outArray);
		case kSecFormatNetscapeCertSequence:
			return impExpNetscapeCertImport(mExternal, flags, keyParams, importKeychain,
				outArray);
		default:
			break;
	}
		
	if((mExternType == kSecItemTypeCertificate) || 
	   (mExternType == kSecItemTypeAggregate)) {
		SecImpExpDbg("SecImportRep::importRep screwup");
		return unimpErr;
	}

	/* 
	 * All that's left: keys. 
	 */
	if((mExternType == kSecItemTypePrivateKey) && (keyImportState == PIS_NoMore)) {
		/* multi key import against caller's wishes */
		return errSecMultiplePrivKeys;
	}

	OSStatus ortn = noErr;
	switch(mExternFormat) {
		case kSecFormatOpenSSL:
		case kSecFormatSSH:
		case kSecFormatBSAFE:
		case kSecFormatRawKey:
			ortn = impExpImportRawKey(mExternal, mExternFormat, mExternType,
				mKeyAlg, importKeychain, cspHand, flags, keyParams, outArray);
			break;
		case kSecFormatWrappedPKCS8:
			ortn = impExpPkcs8Import(mExternal, importKeychain, cspHand, flags,
				keyParams, outArray);
			break;
		case kSecFormatWrappedOpenSSL:
			ortn =  importWrappedKeyOpenssl(importKeychain, cspHand, flags, keyParams, 
				outArray);
			break;
		case kSecFormatWrappedSSH:
		case kSecFormatWrappedLSH:
		default:
			return errSecUnknownFormat;
	}
	if((ortn == noErr) && (keyImportState == PIS_AllowOne)) {
		/* reached our limit */
		keyImportState = PIS_NoMore;
	}
	return ortn;
}

