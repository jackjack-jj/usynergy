#include <jni.h>
#include <string.h>

#include "uSynergy.h"
#include "log.h"

extern uSynergyContext uSynergyLinuxContext;


char* jstringTostring(JNIEnv* env, jstring jstr){
	char* rtn = NULL;
	jclass clsstring = (*env)->FindClass(env,"java/lang/String");
	jstring strencode = (*env)->NewStringUTF(env,"utf-8");
	jmethodID mid = (*env)->GetMethodID(env,clsstring, "getBytes", "(Ljava/lang/String;)[B");
	jbyteArray barr= (jbyteArray)(*env)->CallObjectMethod(env,jstr, mid, strencode);
	jsize alen = (*env)->GetArrayLength(env,barr);
	jbyte* ba = (*env)->GetByteArrayElements(env,barr, JNI_FALSE);
	if (alen > 0){
		rtn = (char*)malloc(alen + 1);
		memcpy(rtn, ba, alen);
		rtn[alen] = 0;
	}
	(*env)->ReleaseByteArrayElements(env,barr, ba, 0);
	return rtn;
}

//char* to jstring
jstring stoJstring(JNIEnv* env, const char* pat){
	jclass strClass = (*env)->FindClass(env,"java/lang/String");
	jmethodID ctorID = (*env)->GetMethodID(env,strClass, "<init>", "([BLjava/lang/String;)V");
	jbyteArray bytes = (*env)->NewByteArray(env,strlen(pat));
	(*env)->SetByteArrayRegion(env,bytes, 0, strlen(pat), (jbyte*)pat);
	jstring encoding = (*env)->NewStringUTF(env,"utf-8");
	return (jstring)(*env)->NewObject(env,strClass, ctorID, bytes, encoding);
}


/*
 * init() Initialization
 * jint  0:success 1:faild
 */
jint Java_io_brotherhood_usynergy_service_UsynergyService_init(JNIEnv *env, jobject thiz,jstring screenName,jint hight,jint width)
{
    int m_Width = width;
	int m_Height = hight;
	char* name = jstringTostring(env,screenName);
	LOGI("init = %s:%d*%d",name, m_Height, m_Width);

	uSynergyInit(&uSynergyLinuxContext,name , m_Height, m_Width);
}
/**
 * getclipBoard Text
 * jstring clipboard Text
 */
jstring Java_io_brotherhood_usynergy_service_UsynergyService_getClipBoardText(JNIEnv *env,jobject thiz){

//	return null;
}

/*
 * start()
 * jint  0:success 1:faild
 */
jint Java_io_brotherhood_usynergy_service_UsynergyService_start(JNIEnv *env, jobject thiz,jstring ip,jint port)
{
	char* ipStr;
	ipStr = malloc(strlen(jstringTostring(env,ip)) + 1);

	uSynergyLinuxContext.m_cookie->ipAddr = strcpy(ipStr, jstringTostring(env,ip));
	uSynergyLinuxContext.m_cookie->port = port;
	LOGI("start = %s:%d", ipStr, port);
	uSynergyStart(&uSynergyLinuxContext);
}

/*
 * shutdown()
 * jint  0:success 1:faild
 */
jint Java_io_brotherhood_usynergy_service_UsynergyService_shutdown(JNIEnv *env,jobject thiz)
{
	uSynergyStop(&uSynergyLinuxContext);
}

/*
 * setClientName() set Client Name
 * jint  0:success 1:faild
 */
jint Java_io_brotherhood_usynergy_service_UsynergyService_setClientName(JNIEnv *env,jobject thiz, jstring clientName,jint height,jint width)
{
	uSynergCleanUP(&uSynergyLinuxContext);
//	uSynergyInit(&uSynergyLinuxContext, clientName, 1024, 600);
	uSynergyInit(&uSynergyLinuxContext, clientName, height, width);
}

jint Java_io_brotherhood_usynergy_service_UsynergyService_exit(JNIEnv *env,jobject thiz)
{
	uSynergCleanUP(&uSynergyLinuxContext);
}

jint Java_io_brotherhood_usynergy_service_getX(JNIEnv *env,jobject thiz){

}

jint Java_io_brotherhood_usynergy_service_getY(JNIEnv *env,jobject thiz){

}
