/*
  Overwolf Sample Plugin
  Copyright (c) 2014 Ovefwolf Ltd.
*/
#include "nsScriptableObjectOverwolfSample.h"
#include "utils/Thread.h"
#include <Windows.h>
#include <sstream>
#include <codecvt>

#define REGISTER_METHOD(name, method) { \
  methods_[NPN_GetStringIdentifier(name)] = &method; \
}

struct LinkedMem {
	uint32_t uiVersion;
	uint32_t uiTick;
	float	fAvatarPosition[3];
	float	fAvatarFront[3];
	float	fAvatarTop[3];
	wchar_t	name[256];
	float	fCameraPosition[3];
	float	fCameraFront[3];
	float	fCameraTop[3];
	wchar_t	identity[256];
	uint32_t context_len;
	unsigned char context[256];
	wchar_t description[2048];
};

const char kAPropretyName[] = "sampleProperty";

HANDLE hMapObject = CreateFileMappingW(INVALID_HANDLE_VALUE, NULL, PAGE_READWRITE, 0, sizeof(LinkedMem), L"MumbleLink");

nsScriptableObjectOverwolfSample::nsScriptableObjectOverwolfSample(NPP npp) :
  nsScriptableObjectBase(npp),
  sample_property_(0.0),
  shutting_down_(false) {
}

nsScriptableObjectOverwolfSample::~nsScriptableObjectOverwolfSample(void) {
  shutting_down_ = true;
  
  if (thread_.get()) {
    thread_->Stop();
  }
}

bool nsScriptableObjectOverwolfSample::Init() {
  REGISTER_METHOD("echo", nsScriptableObjectOverwolfSample::Echo);
  REGISTER_METHOD("add", nsScriptableObjectOverwolfSample::Add);
  REGISTER_METHOD("getGW2MemMappedData", nsScriptableObjectOverwolfSample::GetGW2MemMappedData);

  sample_property_ = 42;

  thread_.reset(new utils::Thread());
  return thread_->Start();
}

bool nsScriptableObjectOverwolfSample::HasMethod(NPIdentifier name) {
#ifdef _DEBUG
  NPUTF8* name_utf8 = NPN_UTF8FromIdentifier(name);
  NPN_MemFree((void*)name_utf8);
#endif

  // does the method exist?
  return (methods_.find(name) != methods_.end());
}

bool nsScriptableObjectOverwolfSample::Invoke(
  NPIdentifier name, 
  const NPVariant *args, 
  uint32_t argCount, 
  NPVariant *result) {
#ifdef _DEBUG
      NPUTF8* szName = NPN_UTF8FromIdentifier(name);
      NPN_MemFree((void*)szName);
#endif

  // dispatch method to appropriate handler
  MethodsMap::iterator iter = methods_.find(name);
  
  if (iter == methods_.end()) {
    // should never reach here
    NPN_SetException(this, "bad function called??");
    return false;
  }

  return (this->*iter->second)(name, args, argCount, result);
}

/************************************************************************/
/* Public properties
/************************************************************************/
bool nsScriptableObjectOverwolfSample::HasProperty(NPIdentifier name) {
  return (NPN_GetStringIdentifier(kAPropretyName) == name);
}

// NOTE: you could also implement SetProperty which receives a callback
// function (as an NPObject) and then you can trigger this callback
// with nsScriptableObjectOverwolfSample::TriggerCallbackOnMainThread.
// This allows you to implement a "global" event for the plugin
bool nsScriptableObjectOverwolfSample::GetProperty(
  NPIdentifier name, NPVariant *result) {

  DOUBLE_TO_NPVARIANT(sample_property_, *result);
  return true;
}

bool nsScriptableObjectOverwolfSample::SetProperty(
  NPIdentifier name, const NPVariant *value) {
  NPN_SetException(this, "this property is read-only!");
  return true;
}


/************************************************************************/
/* Public methods
/************************************************************************/
bool nsScriptableObjectOverwolfSample::Echo(
  NPIdentifier name, 
  const NPVariant *args, 
  uint32_t argCount, 
  NPVariant *result) {

  if (argCount < 2 ||
    !NPVARIANT_IS_STRING(args[0]) ||
    !NPVARIANT_IS_OBJECT(args[1])) {
      NPN_SetException(this, "invalid params passed to function");
      return true;
  }

  // add ref count to callback object so it won't delete
  NPN_RetainObject(NPVARIANT_TO_OBJECT(args[1]));

  // convert into std::string
  std::string message;
  message.append(
    NPVARIANT_TO_STRING(args[0]).UTF8Characters,
    NPVARIANT_TO_STRING(args[0]).UTF8Length);

  // post to separate thread so that we are responsive
  return thread_->PostTask(
    std::bind(
      &nsScriptableObjectOverwolfSample::EchoTask, 
      this, 
      message, 
      NPVARIANT_TO_OBJECT(args[1])));
}

bool nsScriptableObjectOverwolfSample::Add(
  NPIdentifier name, 
  const NPVariant *args, 
  uint32_t argCount, 
  NPVariant *result) {

  if (argCount < 3 ||
      !NPVARIANT_IS_DOUBLE(args[0]) ||
      !NPVARIANT_IS_DOUBLE(args[1]) ||
      !NPVARIANT_IS_OBJECT(args[2])) {
    NPN_SetException(this, "invalid params passed to function");
    return true;
  }

  // add ref count to callback object so it won't delete
  NPN_RetainObject(NPVARIANT_TO_OBJECT(args[2]));

  // post to separate thread so that we are responsive
  return thread_->PostTask(
    std::bind(
      &nsScriptableObjectOverwolfSample::AddTask, 
      this, 
      NPVARIANT_TO_DOUBLE(args[0]), 
      NPVARIANT_TO_DOUBLE(args[1]), 
      NPVARIANT_TO_OBJECT(args[2])));
}

bool nsScriptableObjectOverwolfSample::GetGW2MemMappedData(
	NPIdentifier name,
	const NPVariant *args,
	uint32_t argCount,
	NPVariant *result) {

	if (argCount < 1 ||
		!NPVARIANT_IS_OBJECT(args[0])) {
		NPN_SetException(this, "invalid params passed to function");
		return true;
	}

	// add ref count to callback object so it won't delete
	NPN_RetainObject(NPVARIANT_TO_OBJECT(args[0]));

	// post to separate thread so that we are responsive
	return thread_->PostTask(
		std::bind(
		&nsScriptableObjectOverwolfSample::GetGW2MemMappedDataTask,
		this,
		NPVARIANT_TO_OBJECT(args[0])));
}

std::string wstring_to_utf8(const std::wstring& str)
{
	std::wstring_convert<std::codecvt_utf8<wchar_t>> myconv;
	return myconv.to_bytes(str);
}


/************************************************************************/
/* Separate thrad implementations for public functions
/************************************************************************/
// this is not called on the main thread - so we need to 
// use NPN_PluginThreadAsyncCall
void nsScriptableObjectOverwolfSample::GetGW2MemMappedDataTask(NPObject* callback) {

	if (shutting_down_) {
		return;
	}

	NPVariant arg;
	NPVariant ret_val;

	LinkedMem* lm;

	if (hMapObject == NULL){
		hMapObject = CreateFileMappingW(INVALID_HANDLE_VALUE, NULL, PAGE_READWRITE, 0, sizeof(LinkedMem), L"MumbleLink");
	}

	std::wstringstream json;
	std::string message = "{\"error\": \"Failed to GW2 Data from Memory\"}";
	if (hMapObject != NULL) {
		lm = (LinkedMem*)MapViewOfFile(hMapObject, FILE_MAP_ALL_ACCESS, 0, 0, sizeof(LinkedMem));
		json << "{" << std::endl;

		//Player
		json << "\"player\": {" << std::endl;
		//Avatar Position
		json << "\"position\": {" << std::endl;
		json << "\"x\": " << lm->fAvatarPosition[0] << "," << std::endl;
		json << "\"y\": " << lm->fAvatarPosition[1] << "," << std::endl;
		json << "\"z\": " << lm->fAvatarPosition[2] << std::endl;
		json << "},";

		//Avatar Front
		json << "\"front\": {" << std::endl;
		json << "\"x\": " << lm->fAvatarFront[0] << "," << std::endl;
		json << "\"y\": " << lm->fAvatarFront[1] << "," << std::endl;
		json << "\"z\": " << lm->fAvatarFront[2] << std::endl;
		json << "},";

		//Avatar Top
		json << "\"top\": {" << std::endl;
		json << "\"x\": " << lm->fAvatarTop[0] << "," << std::endl;
		json << "\"y\": " << lm->fAvatarTop[1] << "," << std::endl;
		json << "\"z\": " << lm->fAvatarTop[2] << std::endl;
		json << "}" << std::endl;

		json << "}," << std::endl;

		//Camera
		json << "\"camera\": {" << std::endl;
		//Avatar Position
		json << "\"position\": {" << std::endl;
		json << "\"x\": " << lm->fCameraPosition[0] << "," << std::endl;
		json << "\"y\": " << lm->fCameraPosition[1] << "," << std::endl;
		json << "\"z\": " << lm->fCameraPosition[2] << std::endl;
		json << "},";

		//Avatar Front
		json << "\"front\": {" << std::endl;
		json << "\"x\": " << lm->fCameraFront[0] << "," << std::endl;
		json << "\"y\": " << lm->fCameraFront[1] << "," << std::endl;
		json << "\"z\": " << lm->fCameraFront[2] << std::endl;
		json << "},";

		//Avatar Top
		json << "\"top\": {" << std::endl;
		json << "\"x\": " << lm->fCameraTop[0] << "," << std::endl;
		json << "\"y\": " << lm->fCameraTop[1] << "," << std::endl;
		json << "\"z\": " << lm->fCameraTop[2] << std::endl;
		json << "}" << std::endl;

		json << "}," << std::endl;

		
		json << "\"identity\": " << lm->identity << std::endl;
		json << std::endl << "}";
		if (lm == NULL){
			CloseHandle(hMapObject);
			hMapObject = NULL;
		}

	}
	/*
	if (hMapObject != NULL) {
		lm = (LinkedMem*)MapViewOfFile(hMapObject, FILE_MAP_ALL_ACCESS, 0, 0, sizeof(LinkedMem));
		if (lm != NULL){
			json << "{" << std::endl;

				//Player
				json << "\"player\": {" << std::endl;
					//Avatar Position
					json << "\"position\": {" << std::endl;
						json << "\"x\": " << lm->fAvatarPosition[0] << "," << std::endl;
						json << "\"y\": " << lm->fAvatarPosition[1] << "," << std::endl;
						json << "\"z\": " << lm->fAvatarPosition[2] << std::endl;
					json << "},";

					//Avatar Front
					json << "\"front\": {" << std::endl;
						json << "\"x\": " << lm->fAvatarFront[0] << "," << std::endl;
						json << "\"y\": " << lm->fAvatarFront[1] << "," << std::endl;
						json << "\"z\": " << lm->fAvatarFront[2] << std::endl;
					json << "},";

					//Avatar Top
					json << "\"top\": {" << std::endl;
						json << "\"x\": " << lm->fAvatarTop[0] << "," << std::endl;
						json << "\"y\": " << lm->fAvatarTop[1] << "," << std::endl;
						json << "\"z\": " << lm->fAvatarTop[2] << std::endl;
					json << "}"<<std::endl;
					
				json << "}," << std::endl;

				//Camera
				json << "\"camera\": {" << std::endl;
					//Avatar Position
					json << "\"position\": {" << std::endl;
						json << "\"x\": " << lm->fAvatarPosition[0] << "," << std::endl;
						json << "\"y\": " << lm->fAvatarPosition[1] << "," << std::endl;
						json << "\"z\": " << lm->fAvatarPosition[2] << std::endl;
					json << "},";

					//Avatar Front
					json << "\"front\": {" << std::endl;
						json << "\"x\": " << lm->fAvatarFront[0] << "," << std::endl;
						json << "\"y\": " << lm->fAvatarFront[1] << "," << std::endl;
						json << "\"z\": " << lm->fAvatarFront[2] << std::endl;
					json << "},";

					//Avatar Top
					json << "\"top\": {" << std::endl;
						json << "\"x\": " << lm->fAvatarTop[0] << "," << std::endl;
						json << "\"y\": " << lm->fAvatarTop[1] << "," << std::endl;
						json << "\"z\": " << lm->fAvatarTop[2] << std::endl;
					json << "}" << std::endl;

				json << "}," << std::endl;

				json << "\"identity\": {" << lm->identity << "}" << std::endl;
			json << std::endl << "}";
			lm = NULL;
		}
		CloseHandle(hMapObject);
		hMapObject = NULL;
	}
	*/
	
	message = wstring_to_utf8(json.str());

	json.str(std::wstring());

	STRINGN_TO_NPVARIANT(
		message.c_str(),
		message.size(),
		arg);

	// fire callback
	NPN_InvokeDefault(
		__super::npp_,
		callback,
		&arg,
		1,
		&ret_val);

	NPN_ReleaseVariantValue(&ret_val);
}

void nsScriptableObjectOverwolfSample::EchoTask(
  const std::string& message, NPObject* callback) {

  if (shutting_down_) {
    return;
  }

  NPVariant arg;
  NPVariant ret_val;

  STRINGN_TO_NPVARIANT(
    message.c_str(),
    message.size(),
    arg);

  // fire callback
  NPN_InvokeDefault(
    __super::npp_, 
    callback, 
    &arg, 
    1, 
    &ret_val);

  NPN_ReleaseVariantValue(&ret_val);
}

void nsScriptableObjectOverwolfSample::AddTask(
  double first, double second, NPObject* callback) {
  if (shutting_down_) {
    return;
  }

  NPVariant arg;
  NPVariant ret_val;

  DOUBLE_TO_NPVARIANT(first+second, arg);

  // fire callback
  NPN_InvokeDefault(
    __super::npp_, 
    callback, 
    &arg, 
    1, 
    &ret_val);

  NPN_ReleaseVariantValue(&ret_val);
}
