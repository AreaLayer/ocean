// Copyright (c) 2018 The CommerceBlock Developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

//A wrapper class for AES256CBCEncryption

#include "ecies.h"
#include "crypto/aes.h"
#include "random.h"
#include <sstream>
#include <iostream>


CECIES::CECIES(){
	//Generate a random initialization vector.
    GetStrongRandBytes(_iv, AES_BLOCKSIZE);
    //Generate a random key.
    GetStrongRandBytes(_k, AES256_KEYSIZE);
    Initialize();
}

//This sets up an encryption scheme based on a private key from one key pair
//and public key from another key pair. A shared secret _k is generated which is
//used to enrypt/decrypt messages.
CECIES::CECIES(const CKey& privKey, const CPubKey& pubKey, const uCharVec iv){
	//Generate the ECDH exchange shared secret from the private key and the public key
	uint256 k = privKey.ECDH(pubKey);
	memcpy(_k, &k, 32);
	set_iv(iv);
//	memcpy(_iv, iv, AES_BLOCKSIZE);
	Initialize();
}

CECIES::CECIES(const CKey& privKey, const CPubKey& pubKey){
	//Generate the ECDH exchange shared secret from the private key and the public key
	uint256 k = privKey.ECDH(pubKey);
	memcpy(_k, &k, 32);
//Randomly generate a initialization vector.
    GetStrongRandBytes(_iv, AES_BLOCKSIZE);
    Initialize();
}

CECIES::~CECIES(){
	delete _encryptor;
	delete _decryptor;
}

bool CECIES::set_iv(uCharVec iv){
	std::copy(iv.begin(), iv.begin() + AES_BLOCKSIZE, _iv);
	return true;
}

//Initialize from serialized private key 
bool CECIES::Initialize(){
	_decryptor=new AES256CBCDecrypt(_k, _iv, true);
	_encryptor=new AES256CBCEncrypt(_k, _iv, true);
	return true;
}

bool CECIES::Encrypt(uCharVec& em, 
 	uCharVec& m) const{
	em.resize(m.size());
	_encryptor->Encrypt(m.data(), m.size(), em.data());
    return true;
}

bool CECIES::Decrypt(uCharVec& m, 
 	uCharVec& em) const{
	m.resize(em.size());
	_decryptor->Decrypt(em.data(), em.size(), m.data());
    return true;
}

bool CECIES::Encrypt(std::string& em, 
 	std::string& m) const{
    return true;
}

bool CECIES::Decrypt(std::string& m, 
 	std::string& em) const{
    return true;
}

uCharVec CECIES::get_iv(){
	uCharVec retval(_iv, _iv+AES_BLOCKSIZE);
	return retval;
}

bool CECIES::Test1(){
	Initialize();
	std::string spm = "Test message for ECIES.";
	std::vector<unsigned char> pm(spm.begin(), spm.end());
	std::vector<unsigned char> em;
	std::vector<unsigned char> dm;
	std::cout << spm << std::endl;
	//EncryptMessage(em, pm);
	std::cout << std::string(em.begin(), em.end()) << std::endl;
//	DecryptMessage(dm, em);
	std::cout << std::string(dm.begin(), dm.end()) << std::endl;
	return true;
}


