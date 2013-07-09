// $Id: AliHLTTPCHWClusterDecoderComponent.cxx 59611 2012-11-15 16:23:28Z sgorbuno $

//***************************************************************************
//* This file is property of and copyright by the ALICE HLT Project         *
//* ALICE Experiment at CERN, All rights reserved.                          *
//*                                                                         *
//* Primary Authors: Sergey Gorbunov <sergey.gorbunov@fias.uni-frankfurt.de *
//*                  for The ALICE HLT Project.                             *
//*                                                                         *
//* Permission to use, copy, modify and distribute this software and its    *
//* documentation strictly for non-commercial purposes is hereby granted    *
//* without fee, provided that the above copyright notice appears in all    *
//* copies and that both the copyright notice and this permission notice    *
//* appear in the supporting documentation. The authors make no claims      *
//* about the suitability of this software for any purpose. It is           *
//* provided "as is" without express or implied warranty.                   *
//***************************************************************************


/** @file   AliHLTTPCHWClusterDecoderComponent.cxx
    @author Sergey Gorbunov
    @date   
    @brief 
*/

#include "AliHLTTPCHWClusterDecoderComponent.h"
#include "AliHLTTPCHWClusterMergerV1.h"
#include "AliHLTTPCTransform.h"
#include "AliHLTTPCDefinitions.h"
#include "AliHLTTPCSpacePointData.h"
#include "AliHLTTPCClusterDataFormat.h"
#include "AliRawDataHeader.h"
#include "AliHLTTPCRawCluster.h"
#include "AliHLTTPCHWCFEmulator.h"
#include "AliHLTTPCHWCFData.h"
#include "AliHLTErrorGuard.h"

#include "AliCDBManager.h"
#include "AliCDBEntry.h"

#include "TMath.h"
#include "TObjString.h" 
#include <cstdlib>
#include <cerrno>
#include <sys/time.h>

using namespace std;

ClassImp(AliHLTTPCHWClusterDecoderComponent) //ROOT macro for the implementation of ROOT specific class methods

const char* AliHLTTPCHWClusterDecoderComponent::fgkOCDBEntry="HLT/ConfigTPC/TPCHWClusterDecoder";


AliHLTTPCHWClusterDecoderComponent::AliHLTTPCHWClusterDecoderComponent()
:
fpDecoder(NULL),
fpClusterMerger(NULL),
fDoMerge(1),
fBenchmark("HWClusterDecoder")
{
  // see header file for class documentation
  // or
  // refer to README to build package
  // or
  // visit http://web.ift.uib.no/~kjeks/doc/alice-hlt  

  fBenchmark.Reset();
  fBenchmark.SetTimer(0,"total");
}

AliHLTTPCHWClusterDecoderComponent::~AliHLTTPCHWClusterDecoderComponent()
{ 
  // destructor
  delete fpDecoder;
  delete fpClusterMerger;
}

const char* AliHLTTPCHWClusterDecoderComponent::GetComponentID() 
{ 
  // see header file for class documentation
  return "TPCHWClusterDecoder";
}



void AliHLTTPCHWClusterDecoderComponent::GetInputDataTypes( vector<AliHLTComponentDataType>& list) { 
  // see header file for class documentation

  list.clear(); 
  list.push_back( AliHLTTPCDefinitions::fgkHWClustersDataType | kAliHLTDataOriginTPC  );
  list.push_back( AliHLTTPCDefinitions::fgkAliHLTDataTypeClusterMCInfo | kAliHLTDataOriginTPC );
}

AliHLTComponentDataType AliHLTTPCHWClusterDecoderComponent::GetOutputDataType() { 
  // see header file for class documentation

  return kAliHLTMultipleDataType;
}

int AliHLTTPCHWClusterDecoderComponent::GetOutputDataTypes(AliHLTComponentDataTypeList& tgtList) { 
  // see header file for class documentation

  tgtList.clear();
  tgtList.push_back( AliHLTTPCDefinitions::fgkRawClustersDataType  | kAliHLTDataOriginTPC );
  tgtList.push_back( AliHLTTPCDefinitions::fgkAliHLTDataTypeClusterMCInfo | kAliHLTDataOriginTPC );
  return tgtList.size();
}

void AliHLTTPCHWClusterDecoderComponent::GetOutputDataSize( unsigned long& constBase, double& inputMultiplier ) { 
  // see header file for class documentation
  constBase = 0;
  inputMultiplier = 2.0;
}

AliHLTComponent* AliHLTTPCHWClusterDecoderComponent::Spawn() { 
  // see header file for class documentation

  return new AliHLTTPCHWClusterDecoderComponent();
}
	
int AliHLTTPCHWClusterDecoderComponent::DoInit( int argc, const char** argv ) 
{ 
  // see header file for class documentation
  
  int iResult=0;

  fpDecoder=new AliHLTTPCHWCFData;
  if (!fpDecoder) iResult=-ENOMEM;
  
  fDoMerge = 1;

  //!! if (iResult>=0) iResult = ConfigureFromCDBTObjString(fgkOCDBEntry);

  if (iResult>=0 && argc>0) iResult = ConfigureFromArgumentString(argc, argv);

  if ( iResult>=0 ) iResult = InitClusterMerger();  
  
  return iResult;
} // end DoInit()

int AliHLTTPCHWClusterDecoderComponent::DoDeinit() { 
  // see header file for class documentation   
  if (!fpDecoder) delete fpDecoder;
  fpDecoder=NULL;
  delete fpClusterMerger;
  fpClusterMerger = NULL;
  return 0;
}

int AliHLTTPCHWClusterDecoderComponent::Reconfigure(const char* /*cdbEntry*/, const char* /*chainId*/) { 
  // see header file for class documentation

  fDoMerge = 1;
  int iResult = 0;
  //!! iResult = ConfigureFromCDBTObjString(fgkOCDBEntry);
  if ( iResult>=0 ) iResult = InitClusterMerger();  
  return iResult;
}

int AliHLTTPCHWClusterDecoderComponent::ScanConfigurationArgument(int argc, const char** argv){

  // see header file for class documentation

  if (argc<=0) return 0;
  int i=0;
  TString argument=argv[i];

  if (argument.CompareTo("-do-merge")==0){
    fDoMerge = 1;
    return 1;
  }

  if (argument.CompareTo("-do-not-merge")==0){
    fDoMerge = 0;
    return 1;
  }

  // unknown argument
  return -EINVAL;
}

int AliHLTTPCHWClusterDecoderComponent::InitClusterMerger()
{
  //
  // init merger
  //
  int iResult = 0;
  if ( fDoMerge && !fpClusterMerger) {
    fpClusterMerger = new AliHLTTPCHWClusterMergerV1;
    if( !fpClusterMerger ) iResult=-ENOMEM;
    else iResult = fpClusterMerger->Init();
    if( iResult<0 ){
      HLTError("Can not initialise cluster merger");
      delete fpClusterMerger;
      fpClusterMerger = 0;
    }
  }
  return iResult;
}

void AliHLTTPCHWClusterDecoderComponent::GetOCDBObjectDescription( TMap* const targetMap)
{
  // Get a list of OCDB object description needed for the particular component
  if (!targetMap) return;
  
  // OCDB entries for component arguments

  //!! targetMap->Add(new TObjString(fgkOCDBEntry), new TObjString("component argument for HW cluster decoder"));  
}


int AliHLTTPCHWClusterDecoderComponent::DoEvent(const AliHLTComponentEventData& evtData, 
					          const AliHLTComponentBlockData* blocks, 
					          AliHLTComponentTriggerData& /*trigData*/, AliHLTUInt8_t* outputPtr, 
					          AliHLTUInt32_t& size, 
					          vector<AliHLTComponentBlockData>& outputBlocks ){
  // see header file for class documentation
 
  UInt_t maxOutSize = size;
  size = 0;
  int iResult = 0;
  if(!IsDataEvent()) return 0;

  if (!fpDecoder) return -ENODEV;

  fBenchmark.StartNewEvent();
  fBenchmark.Start(0);
  
  AliHLTUInt8_t* origOutputPtr = outputPtr;
  UInt_t origOutputBlocksSize = outputBlocks.size();

  for( unsigned long ndx=0; ndx<evtData.fBlockCnt; ndx++ ){
     
    const AliHLTComponentBlockData *iter   = blocks+ndx;
    
    fBenchmark.AddInput(iter->fSize);
    
    HLTDebug("Event 0x%08LX (%Lu) received datatype: %s - required datatype: %s",
	     evtData.fEventID, evtData.fEventID, 
	     DataType2Text( iter->fDataType).c_str(), 
	     DataType2Text(AliHLTTPCDefinitions::fgkHWClustersDataType).c_str());                       
 
    if(iter->fDataType == (AliHLTTPCDefinitions::fgkAliHLTDataTypeClusterMCInfo | kAliHLTDataOriginTPC) ){
      // simply forward MC labels
      
      if( size+iter->fSize > maxOutSize ){
	HLTWarning( "Output buffer (%db) is too small, required %db", maxOutSize, size+iter->fSize);
	iResult  = -ENOSPC;
	break;
      }

      memcpy( outputPtr, iter->fPtr, iter->fSize );
      
      AliHLTComponentBlockData bd;
      FillBlockData( bd );
      bd.fOffset = size;
      bd.fSize = iter->fSize;      
      bd.fSpecification = iter->fSpecification;     
      bd.fDataType = iter->fDataType;
      outputBlocks.push_back( bd );           
      size   += bd.fSize;
      outputPtr += bd.fSize;
      fBenchmark.AddOutput(bd.fSize);    
      continue;
    }

    if( iter->fDataType == (AliHLTTPCDefinitions::fgkHWClustersDataType | kAliHLTDataOriginTPC) ){
        
      UInt_t minSlice     = AliHLTTPCDefinitions::GetMinSliceNr(*iter); 
      UInt_t minPartition = AliHLTTPCDefinitions::GetMinPatchNr(*iter);
      
      fBenchmark.SetName(Form("HWClusterTransform slice %d patch %d",minSlice,minPartition));
      HLTDebug("minSlice: %d, minPartition: %d", minSlice, minPartition);     
      
      long maxRawClusters = ((long)maxOutSize-size-sizeof(AliHLTTPCRawClusterData))/sizeof(AliHLTTPCRawCluster);
       
      if( maxRawClusters<=0 ) {
	HLTWarning("No more space to add raw clusters, exiting!");
	iResult  = -ENOSPC;
	continue;
      }       

      // copy raw cluster data from input
	 
      AliHLTTPCRawClusterData* outputRaw= (AliHLTTPCRawClusterData*)(outputPtr);
       
      outputRaw->fVersion = 0;
      outputRaw->fCount = 0;
      
      AliHLTUInt32_t *buffer = (AliHLTUInt32_t*)iter->fPtr;  
     
      // skip the first 8 32-bit CDH words
      buffer += 8;
      UInt_t bufferSize32 = ((Int_t)iter->fSize - sizeof(AliRawDataHeader) )/sizeof(AliHLTUInt32_t);
      
      if (fpDecoder->Init(reinterpret_cast<AliHLTUInt8_t*>(buffer), bufferSize32*sizeof(AliHLTUInt32_t))>=0 && fpDecoder->CheckVersion()>=0) {
	
	for (AliHLTTPCHWCFData::iterator cl=fpDecoder->begin(); cl!=fpDecoder->end(); ++cl) {
	     
	  if(outputRaw->fCount>=maxRawClusters){
	    HLTWarning("No more space to add clusters, exiting!");
	    iResult  = -ENOSPC;
	    break;
	  }
	  AliHLTTPCRawCluster &c = outputRaw->fClusters[outputRaw->fCount];	  
	  c.SetPadRow(cl.GetPadRow());
	  c.SetCharge(cl.GetCharge());
	  c.SetPad(cl.GetPad());  
	  c.SetTime(cl.GetTime());
	  c.SetSigmaY2(cl.GetSigmaY2());
	  c.SetSigmaZ2(cl.GetSigmaZ2());
	  c.SetQMax(cl.GetQMax());	  
	  outputRaw->fCount++;
	}	
	
	// fill into HLT output data
	AliHLTComponentBlockData bdRawClusters;
	FillBlockData( bdRawClusters );
	bdRawClusters.fOffset = size;	
	bdRawClusters.fSize = sizeof(AliHLTTPCRawClusterData)+outputRaw->fCount*sizeof(AliHLTTPCRawCluster);
	bdRawClusters.fSpecification = iter->fSpecification;
	bdRawClusters.fDataType = AliHLTTPCDefinitions::fgkRawClustersDataType | kAliHLTDataOriginTPC;
	outputBlocks.push_back( bdRawClusters );
	fBenchmark.AddOutput(bdRawClusters.fSize);
	size   += bdRawClusters.fSize;
	outputPtr += bdRawClusters.fSize;
      }    
      continue; // HW clusters data block
    }

  } // end of loop over data blocks  

  if( fDoMerge && fpClusterMerger ){
    fpClusterMerger->Clear();
    fpClusterMerger->SetDataPointer(origOutputPtr);
    for( UInt_t i=origOutputBlocksSize; i<outputBlocks.size(); i++){
      fpClusterMerger->SetDataBlock(&(outputBlocks[i]));
    }
    int nMerged = fpClusterMerger->Merge();
    fpClusterMerger->Clear();
    HLTInfo("Merged %d clusters",nMerged);   
  }
 
  fBenchmark.Stop(0);
  HLTInfo(fBenchmark.GetStatistics());
  
  return iResult;
} // end DoEvent()



void AliHLTTPCHWClusterDecoderComponent::PrintDebug(AliHLTUInt32_t *buffer, Int_t size){
// see header file for class documentation 

  HLTInfo("The size is: %d", size);
  for(Int_t n32bit=0; n32bit<size; n32bit++){
    
    AliHLTUInt8_t *wordPtr = reinterpret_cast<AliHLTUInt8_t*>(&buffer[n32bit]);
    //    cout << "word ptr initialized"<<endl;
    for(Int_t w=3;w>=0;w--){
      //     cout <<"accessing word"<<endl;
      AliHLTUInt8_t word = wordPtr[w];
      //     cout<< "word was accessed"<<endl; 
      for(int n=7; n>=0; n--){
	//print the byte values
	if((((word>>n)<<7)&0x80) != 0){
	  printf("1");
	}
	else{
	  printf("0");
	}
      }
      printf("  ");
    }
    printf("\n");
  }
} // end of PrintDebug
