// Copyright (c) 2018 The CommerceBlock Developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include "whitelist.h"
#include "validation.h"
#include "wallet/wallet.h"
#include "ecies.h"

CWhiteList::CWhiteList(){;}
CWhiteList::~CWhiteList(){;}

void CWhiteList::add_derived(const CBitcoinAddress& address, const CPubKey& pubKey){
  CWhiteList::add_derived(address, pubKey, nullptr);
}

void CWhiteList::add_derived(const CBitcoinAddress& address,  const CPubKey& pubKey, 
  const CBitcoinAddress* kycAddress){
  boost::recursive_mutex::scoped_lock scoped_lock(_mtx);
  if (!pubKey.IsFullyValid())
    throw std::system_error(
          std::error_code(CPolicyList::Errc::INVALID_ADDRESS_OR_KEY, std::system_category()),
          std::string(std::string(__func__) +  ": invalid public key"));

    //Will throw an error if address is not a valid derived address.
  CKeyID keyId;
  if (!address.GetKeyID(keyId))
    throw std::system_error(
          std::error_code(CPolicyList::Errc::INVALID_ADDRESS_OR_KEY, std::system_category()),
          std::string(__func__) + ": invalid key id");

  CKeyID kycKeyId;
  if(kycAddress){
    if (!kycAddress->GetKeyID(kycKeyId))
      throw std::system_error(
			    std::error_code(CPolicyList::Errc::INVALID_ADDRESS_OR_KEY,std::system_category()),
            std::string(__func__) + ": invalid key id (kyc address)");
  }

  if(!Consensus::CheckValidTweakedAddress(keyId, pubKey)) return;

  //Check the KYC key is whitelisted
  if(!find_kyc(kycKeyId)){
    throw std::system_error(
          std::error_code(CPolicyList::Errc::INVALID_ADDRESS_OR_KEY,std::system_category()),
            std::string(__func__) + ": the KYC key is not currently whitelisted.");
    return;
  }

  //insert new address into sorted CWhiteList vector
  add_sorted(&keyId);

  //Add to the ID map
  _kycMap[keyId]=kycKeyId;
  //Used for decryption
  CPubKey tweakedPubKey(pubKey);
   uint256 contract = chainActive.Tip() ? chainActive.Tip()->hashContract : GetContractHash();
  if (!contract.IsNull())
    tweakedPubKey.AddTweakToPubKey((unsigned char*)contract.begin());
    _tweakedPubKeyMap[keyId]=tweakedPubKey;
}

bool CWhiteList::find_kyc(const CKeyID& keyId){
   return(_kycSet.find(keyId) != _kycSet.end());
}

bool CWhiteList::find_kyc(const CPubKey& pubKey){
   return find_kyc(pubKey.GetID());
}

void CWhiteList::add_derived(const std::string& addressIn, const std::string& key){
  add_derived(addressIn, key, std::string(""));
}

void CWhiteList::add_derived(const std::string& sAddress, const std::string& sPubKey, 
  const std::string& sKYCAddress){
   boost::recursive_mutex::scoped_lock scoped_lock(_mtx);
    CBitcoinAddress address;
  if (!address.SetString(sAddress))
    throw std::invalid_argument(std::string(std::string(__func__) + 
      ": invalid Bitcoin address: ") + sAddress);
  
  std::vector<unsigned char> pubKeyData(ParseHex(sPubKey));
  CPubKey pubKey = CPubKey(pubKeyData.begin(), pubKeyData.end());

  CBitcoinAddress* kycAddress;
  if(sKYCAddress.size() == 0) {
    kycAddress = nullptr;
  } else {
    kycAddress = new CBitcoinAddress();
     if (!kycAddress->SetString(sKYCAddress))
     throw std::invalid_argument(std::string(std::string(__func__) + 
      ": invalid Bitcoin address (kyc key): ") + sKYCAddress);
  }
  add_derived(address, pubKey, kycAddress);
  delete kycAddress;
}

void CWhiteList::add_kyc(const CBitcoinAddress& address){
  CKeyID id;
  address.GetKeyID(id);
  add_kyc(id);
}

void CWhiteList::remove_kyc(const CBitcoinAddress& address){
  CKeyID id;
  address.GetKeyID(id);
  remove_kyc(id);
}

void CWhiteList::add_kyc(const CKeyID& keyId){
  _kycSet.insert(keyId);
}

void CWhiteList::remove_kyc(const CKeyID& keyId){
  _kycSet.erase(keyId);
}

bool CWhiteList::RegisterAddress(const CTransaction& tx, const CCoinsViewCache& mapInputs){
  //Check if this is a ID registration (whitetoken) transaction
  // Get input addresses an lookup associated idpubkeys
  if (tx.IsCoinBase())
    return false; // Coinbases don't use vin normally

  //Check if this is a TX_REGISTERADDRESS. If so, read the data into a byte vector.
  opcodetype opcode;
  std::vector<unsigned char> bytes;

  BOOST_FOREACH (const CTxOut& txout, tx.vout) {
    std::vector<std::vector<unsigned char> > vSolutions;
    txnouttype whichType;
    //Is the output solvable?
    if (!Solver(txout.scriptPubKey, whichType, vSolutions)) return false;
    //Is it a TX_REGISTERADDRESS?
    if(whichType == TX_REGISTERADDRESS) {
      CScript::const_iterator pc = txout.scriptPubKey.begin();
      //Can the bytes be read?
      if (!txout.scriptPubKey.GetOp(++pc, opcode, bytes)) return false;
      break;
    }
  }

  //Confirm data read from the TX_REGISTERADDRESS
  if(bytes.size()==0) return false;

  unsigned int pubKeySize=33;
  unsigned int addrSize=20;

  //Are the first 33 bytes a currently whitelisted KYC public key? 
  //If so, this is an initial onboarding transaction, and those 33 bytes are the server KYC public key.
  //And the following 33 bytes are the client onboarding public key.
  std::vector<unsigned char>::const_iterator it=bytes.begin();
  CPubKey kycPubKey = CPubKey(it, it=it+pubKeySize);
  CPubKey userOnboardPubKey = CPubKey(it, it=it+pubKeySize);
  CKeyID kycKey, keyId, onboardKeyID;
  CKey userOnboardPrivKey;
  CPubKey inputPubKey;
  std::set<CPubKey> inputPubKeys;
  bool bOnboard=false;

  if(kycPubKey.IsFullyValid()){
    kycKey=kycPubKey.GetID();
    bOnboard = find_kyc(kycKey);
    // Check if reading from the client node
    if(!bOnboard){
      if(userOnboardPubKey.IsFullyValid()){
        if(pwalletMain->GetKey(userOnboardPubKey.GetID(), userOnboardPrivKey)){  
          bOnboard=true;
        }
      }
    }
  }

  if(bOnboard){
    inputPubKeys.insert(userOnboardPubKey);
    inputPubKey = userOnboardPubKey;
  } else {
    kycPubKey=pwalletMain->GetKYCPubKey();  //For the non-whitelisting nodes
    //Get input keyids
    //Lookup the ID public keys of the input addresses.
    //The set is used to ensure that there is only one kycKey involved.
    BOOST_FOREACH(const CTxIn& prevIn, tx.vin) {
      const CTxOut& prev = mapInputs.GetOutputFor(prevIn);
      CTxDestination dest;
      if(!ExtractDestination(prev.scriptPubKey, dest))
       continue;
    
      // For debugging - translate into bitcoin address
      CBitcoinAddress addr(dest);
      addr.GetKeyID(keyId);
      std::string sAddr = addr.ToString();
      // search in whitelist for the presence of keyid
      // add the associated kycKey to the set of kyc keys
      if(LookupKYCKey(keyId, kycKey)){
        if(find_kyc(kycKey)){ //Is user whitelisted?
          if(LookupTweakedPubKey(keyId, inputPubKey))
            inputPubKeys.insert(inputPubKey);
        }
      }
    }
  }

  if(inputPubKeys.size()!=1) return false;

  //The next AES_BLOCKSIZE bytes of the message are the initialization vector
  //used to decrypt the rest of the message
  it=bytes.begin();
  //std::vector<unsigned char> fromPubKey(it, it+=pubKeySize);
  std::vector<unsigned char> initVec(it, it+=AES_BLOCKSIZE);
  std::vector<unsigned char> encryptedData(it, bytes.end());

  //Get the private key that is paired with kycKey
  CBitcoinAddress kycAddr(kycKey);
  std::string sKYCAddr = kycAddr.ToString();

  // Get the KYC private key from the wallet.
  // This ultimately checks that the kyc key associated with the transaction input address 
  // is already associated with a valid kyc key.
  CPubKey* decryptPubKey;
  CKey decryptPrivKey;
  if(!pwalletMain->GetKey(kycKey, decryptPrivKey)){  
    //Non-whitelisting node
    if(!pwalletMain->GetKey(inputPubKey.GetID(), decryptPrivKey)) return false;  
    decryptPubKey=&kycPubKey;
  } else{
    //Whitelisting node
    decryptPubKey=&inputPubKey;
  }
  
  //Decrypt the data
  //One of the input public keys together with the KYC private key 
  //will compute the shared secret used to encrypt the data

  bool bSuccess=false;

  //Decrypt
  CECIES decryptor(decryptPrivKey, *decryptPubKey, initVec);
  std::vector<unsigned char> data;
  data.resize(encryptedData.size());
  decryptor.Decrypt(data, encryptedData);
    
  //Interpret the data
  //First 20 bytes: keyID 
  std::vector<unsigned char>::const_iterator itData2 = data.begin();
  std::vector<unsigned char>::const_iterator itData1 = itData2;

  std::vector<unsigned char>::const_iterator pend = data.end();
  std::vector<unsigned char> vKeyID, vPubKey;

  bool bEnd=false;
  while(!bEnd){
    for(unsigned int i=0; i<addrSize; ++i){
      if(itData2++ == pend) {
        bEnd=true;
        break;
      }
    }
    if(!bEnd){
      CBitcoinAddress addrNew;
      std::vector<unsigned char> addrChars(itData1,itData2);
      addrNew.Set(CKeyID(uint160(addrChars)));  
      itData1=itData2;
      for(unsigned int i=0; i<pubKeySize; ++i){
        if(itData2++ == pend){
          bEnd=true;
          break;
        }
      }
      if(!bEnd){
        std::vector<unsigned char> pubKeyData(itData1, itData2);
        itData1=itData2;
        CPubKey pubKeyNew = CPubKey(pubKeyData.begin(),pubKeyData.end());
        CBitcoinAddress* addr = new CBitcoinAddress(kycKey);
        //Convert to string for debugging
        std::string sAddr=addr->ToString();
        std::string sAddrNew=addrNew.ToString();
        try{
          add_derived(addrNew, pubKeyNew, addr);
        } catch (std::system_error e){
          if(e.code().value() != CPolicyList::Errc::INVALID_ADDRESS_OR_KEY) throw e;
        }
        delete addr;
        bSuccess=true;
      }
    }
  }
  
  return bSuccess;
}

bool CWhiteList::LookupKYCKey(const CKeyID& address, CKeyID& kycKeyFound){
  auto search = _kycMap.find(address);
  if(search != _kycMap.end()){
    kycKeyFound = search->second;
    return true;
  }
  return false;
}

bool CWhiteList::LookupTweakedPubKey(const CKeyID& address, CPubKey& pubKeyFound){
  auto search = _tweakedPubKeyMap.find(address);
  if(search != _tweakedPubKeyMap.end()){
    pubKeyFound = search->second;
    return true;
  }
  return false;
}

//Update from transaction
bool CWhiteList::Update(const CTransaction& tx, const CCoinsViewCache& mapInputs)
{
    if (tx.IsCoinBase())
      return false; // Coinbases don't use vin normally

    // check inputs for encoded address data
    // The first dummy key in the multisig is the (scrambled) kyc public key.
    for (unsigned int i = 0; i < tx.vin.size(); i++) {
        const CTxOut& prev = mapInputs.GetOutputFor(tx.vin[i]);

        std::vector<std::vector<unsigned char> > vSolutions;
        txnouttype whichType;

        const CScript& prevScript = prev.scriptPubKey;
        if (!Solver(prevScript, whichType, vSolutions)) continue;

        // extract address from second multisig public key and remove from whitelist
        // bytes 0-32: KYC public key assigned by the server, bytes reversed
        
        if (whichType == TX_MULTISIG && vSolutions.size() == 4)
        {
            std::vector<unsigned char> vKycPub(vSolutions[2].begin(), vSolutions[2].begin() + 32);
            //The KYC public key is encoded in reverse to prevent spending, 
            //so we need to reverse the bytes.
            std::reverse(vKycPub.begin(), vKycPub.end());
            CPubKey kycPubKey(vKycPub.begin(), vKycPub.end());
            if (!kycPubKey.IsFullyValid())
                throw std::system_error(
                  std::error_code(CPolicyList::Errc::INVALID_ADDRESS_OR_KEY, std::system_category()),
                  std::string(std::string(__func__) +  ": invalid public key"));

            remove_kyc(kycPubKey.GetID());
        }
    }

    //check outputs for encoded address data
    for (unsigned int i = 0; i < tx.vout.size(); i++) {
        const CTxOut& txout = tx.vout[i];

        std::vector<std::vector<unsigned char> > vSolutions;
        txnouttype whichType;

        if (!Solver(txout.scriptPubKey, whichType, vSolutions)) continue;

        // extract address from second multisig public key and add it to the whitelist
        // bytes 0-32: KYC public key assigned by the server, bytes reversed
        if (whichType == TX_MULTISIG && vSolutions.size() == 4)
        {
            std::vector<unsigned char> vKycPub(vSolutions[2].begin(), vSolutions[2].begin() + 32);
            //The KYC public key is encoded in reverse to prevent spending, 
            //so we need to reverse the bytes.
            std::reverse(vKycPub.begin(), vKycPub.end());
            CPubKey kycPubKey(vKycPub.begin(), vKycPub.end());
            if (!kycPubKey.IsFullyValid())
                throw std::system_error(
                  std::error_code(CPolicyList::Errc::INVALID_ADDRESS_OR_KEY, std::system_category()),
                  std::string(std::string(__func__) +  ": invalid public key"));

            add_kyc(kycPubKey.GetID());

        }
    }
    return true;
}





