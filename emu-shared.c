
#define EMU_MAX_CHAR_KEYNAME 12
#define EMU_KEY_FILENAME "SoftCam.Key"
#define EMU_KEY_FILENAME_MAX_LEN 31
#define EMU_MAX_ECM_LEN 1024
#define EMU_MAX_EMM_LEN 1024

// Version info
uint32_t GetOSemuVersion(void)
{
	return atoi("$Version: 715 $"+10);
}

// Key DB
static char *emu_keyfile_path = NULL;

void set_emu_keyfile_path(char *path)
{
	if(emu_keyfile_path != NULL) {
		free(emu_keyfile_path);
	}
	emu_keyfile_path = (char*)malloc(strlen(path)+1);
	if(emu_keyfile_path == NULL) {
		return;
	}
	memcpy(emu_keyfile_path, path, strlen(path));
	emu_keyfile_path[strlen(path)] = 0;
}

static int32_t CharToBin(uint8_t *out, char *in, uint32_t inLen)
{
	uint32_t i, tmp;
	for(i=0; i<inLen/2; i++) {
		if(sscanf(in + i*2, "%02X", &tmp) != 1) {
			return 0;
		}
		out[i] = (uint8_t)tmp;
	}
	return 1;
}

typedef struct {
	char identifier;
	uint32_t provider;
	char keyName[EMU_MAX_CHAR_KEYNAME];
	uint8_t *key;
	uint32_t keyLength;
	void *nextKey;
} KeyData;

typedef struct {
	KeyData *EmuKeys;
	uint32_t keyCount;
	uint32_t keyMax;
} KeyDataContainer;

static KeyDataContainer CwKeys = { .EmuKeys=NULL, .keyCount=0, .keyMax=0 };
static KeyDataContainer ViKeys = { .EmuKeys=NULL, .keyCount=0, .keyMax=0 };
static KeyDataContainer NagraKeys = { .EmuKeys=NULL, .keyCount=0, .keyMax=0 };
static KeyDataContainer IrdetoKeys = { .EmuKeys=NULL, .keyCount=0, .keyMax=0 };
static KeyDataContainer NDSKeys = { .EmuKeys=NULL, .keyCount=0, .keyMax=0 };
static KeyDataContainer BissKeys = { .EmuKeys=NULL, .keyCount=0, .keyMax=0 };
static KeyDataContainer PowervuKeys = { .EmuKeys=NULL, .keyCount=0, .keyMax=0 };

static KeyDataContainer *GetKeyContainer(char identifier)
{
	switch(identifier) {
	case 'W':
		return &CwKeys;
	case 'V':
		return &ViKeys;
	case 'N':
		return &NagraKeys;
	case 'I':
		return &IrdetoKeys;
	case 'S':
		return &NDSKeys;
	case 'F':
		return &BissKeys;
	case 'P':
		return &PowervuKeys;
	default:
		return NULL;
	}
}

static void WriteKeyToFile(char identifier, uint32_t provider, char *keyName, uint8_t *key, uint32_t keyLength)
{
	char line[1200], dateText[100];
	uint32_t pathLength;
	struct dirent *pDirent;
	DIR *pDir;
	char *path, *filepath, filename[EMU_KEY_FILENAME_MAX_LEN+1], *keyValue;
	FILE *file = NULL;
	uint8_t fileNameLen = strlen(EMU_KEY_FILENAME);
	time_t now;
	struct tm t;

	pathLength = strlen(emu_keyfile_path);
	path = (char*)malloc(pathLength+1);
	if(path == NULL) {
		return;
	}
	strncpy(path, emu_keyfile_path, pathLength+1);

	pathLength = strlen(path);
	if(pathLength >= fileNameLen && strcasecmp(path+pathLength-fileNameLen, EMU_KEY_FILENAME) == 0) {
		// cut file name
		path[pathLength-fileNameLen] = '\0';
	}

	pathLength = strlen(path);
	if(path[pathLength-1] == '/' || path[pathLength-1] == '\\') {
		// cut trailing /
		path[pathLength-1] = '\0';
	}

	pDir = opendir(path);
	if (pDir == NULL) {
		cs_log("cannot open key file path: %s", path);
		free(path);
		return;
	}

	while((pDirent = readdir(pDir)) != NULL) {
		if(strcasecmp(pDirent->d_name, EMU_KEY_FILENAME) == 0) {
			strncpy(filename, pDirent->d_name, sizeof(filename));
			break;
		}
	}
	closedir(pDir);

	if(pDirent == NULL) {
		strncpy(filename, EMU_KEY_FILENAME, sizeof(filename));
	}

	pathLength = strlen(path)+1+strlen(filename)+1;
	filepath = (char*)malloc(pathLength);
	if(filepath == NULL) {
		free(path);
		return;
	}
	snprintf(filepath, pathLength, "%s/%s", path, filename);
	free(path);

	cs_log("writing key file: %s", filepath);

	file = fopen(filepath, "a");
	free(filepath);
	if(file == NULL) {
		return;
	}

	now = time(NULL);
	localtime_r(&now, &t);
	strftime(dateText, sizeof(dateText)-1, "%c", &t);

	keyValue = (char*)malloc((keyLength*2)+1);
	if(keyValue == NULL) {
		return;
	}
	cs_hexdump(0, key, keyLength, keyValue, (keyLength*2)+1);

	snprintf(line, sizeof(line), "\n%c %04X %s %s ; added by OSEmu %s\n", identifier, provider, keyName, keyValue, dateText);
	free(keyValue);

	fwrite(line, strlen(line), 1, file);
	fclose(file);
}

static int32_t SetKey(char identifier, uint32_t provider, char *keyName, uint8_t *key,
					  uint32_t keyLength, uint8_t writeKey)
{
	uint32_t i;
	uint8_t *tmpKey = NULL, *orgKey = NULL;
	KeyDataContainer *KeyDB;
	KeyData *tmpKeyData, *newKeyData;
	identifier = (char)toupper((int)identifier);

	KeyDB = GetKeyContainer(identifier);
	if(KeyDB == NULL) {
		return 0;
	}

	// fix patched mgcamd format for Irdeto
	if(identifier == 'I' && provider < 0xFFFF) {
		provider = provider<<8;
	}

	// fix checksum for biss keys with a length of 6
	if(identifier == 'F' && keyLength == 6) {

		tmpKey = (uint8_t*)malloc(8*sizeof(uint8_t));
		if(tmpKey == NULL) {
			return 0;
		}

		tmpKey[0] = key[0];
		tmpKey[1] = key[1];
		tmpKey[2] = key[2];
		tmpKey[3] = ((key[0] + key[1] + key[2]) & 0xff);
		tmpKey[4] = key[3];
		tmpKey[5] = key[4];
		tmpKey[6] = key[5];
		tmpKey[7] = ((key[3] + key[4] + key[5]) & 0xff);

		orgKey = key;
		key = tmpKey;
		keyLength = 8;
	}

	for(i=0; i<KeyDB->keyCount; i++) {
		if(KeyDB->EmuKeys[i].provider != provider) {
			continue;
		}
		if(strcmp(KeyDB->EmuKeys[i].keyName, keyName)) {
			continue;
		}

		// allow multiple keys for Irdeto
		if(identifier == 'I')
		{
			// reject duplicates
			tmpKeyData = &KeyDB->EmuKeys[i];
			do {
				if(memcmp(tmpKeyData->key, key, tmpKeyData->keyLength < keyLength ? tmpKeyData->keyLength : keyLength) == 0) {
					if(tmpKey != NULL) {
						free(tmpKey);
					}
					return 0;
				}
				tmpKeyData = (KeyData*)tmpKeyData->nextKey;
			}
			while(tmpKeyData != NULL);

			// add new key
			newKeyData = (KeyData*)malloc(sizeof(KeyData));
			if(newKeyData == NULL) {
				if(tmpKey != NULL) {
					free(tmpKey);
				}
				return 0;
			}
			newKeyData->identifier = identifier;
			newKeyData->provider = provider;
			if(strlen(keyName) < EMU_MAX_CHAR_KEYNAME) {
				strncpy(newKeyData->keyName, keyName, EMU_MAX_CHAR_KEYNAME);
			}
			else {
				memcpy(newKeyData->keyName, keyName, EMU_MAX_CHAR_KEYNAME);
			}
			newKeyData->keyName[EMU_MAX_CHAR_KEYNAME-1] = 0;
			newKeyData->key = key;
			newKeyData->keyLength = keyLength;
			newKeyData->nextKey = NULL;

			tmpKeyData = &KeyDB->EmuKeys[i];
			while(tmpKeyData->nextKey != NULL) {
				tmpKeyData = (KeyData*)tmpKeyData->nextKey;
			}
			tmpKeyData->nextKey = newKeyData;

			if(writeKey) {
				WriteKeyToFile(identifier, provider, keyName, key, keyLength);
			}
		}
		else // identifier != 'I'
		{
			free(KeyDB->EmuKeys[i].key);
			KeyDB->EmuKeys[i].key = key;
			KeyDB->EmuKeys[i].keyLength = keyLength;

			if(writeKey) {
				WriteKeyToFile(identifier, provider, keyName, key, keyLength);
			}
		}

		if(tmpKey != NULL) {
			free(orgKey);
		}
		return 1;
	}

	if(KeyDB->keyCount+1 > KeyDB->keyMax) {
		if(KeyDB->EmuKeys == NULL) {
			KeyDB->EmuKeys = (KeyData*)malloc(sizeof(KeyData)*(KeyDB->keyMax+64));
			if(KeyDB->EmuKeys == NULL) {
				if(tmpKey != NULL) {
					free(tmpKey);
				}
				return 0;
			}
			KeyDB->keyMax+=64;
		}
		else {
			tmpKeyData = (KeyData*)realloc(KeyDB->EmuKeys, sizeof(KeyData)*(KeyDB->keyMax+16));
			if(tmpKeyData == NULL) {
				if(tmpKey != NULL) {
					free(tmpKey);
				}
				return 0;
			}
			KeyDB->EmuKeys = tmpKeyData;
			KeyDB->keyMax+=16;
		}
	}

	KeyDB->EmuKeys[KeyDB->keyCount].identifier = identifier;
	KeyDB->EmuKeys[KeyDB->keyCount].provider = provider;
	if(strlen(keyName) < EMU_MAX_CHAR_KEYNAME) {
		strncpy(KeyDB->EmuKeys[KeyDB->keyCount].keyName, keyName, EMU_MAX_CHAR_KEYNAME);
	}
	else {
		memcpy(KeyDB->EmuKeys[KeyDB->keyCount].keyName, keyName, EMU_MAX_CHAR_KEYNAME);
	}
	KeyDB->EmuKeys[KeyDB->keyCount].keyName[EMU_MAX_CHAR_KEYNAME-1] = 0;
	KeyDB->EmuKeys[KeyDB->keyCount].key = key;
	KeyDB->EmuKeys[KeyDB->keyCount].keyLength = keyLength;
	KeyDB->EmuKeys[KeyDB->keyCount].nextKey = NULL;
	KeyDB->keyCount++;

	if(writeKey) {
		WriteKeyToFile(identifier, provider, keyName, key, keyLength);
	}

	if(tmpKey != NULL) {
		free(orgKey);
	}
	return 1;
}

static int32_t FindKey(char identifier, uint32_t provider, char *keyName, uint8_t *key, uint32_t maxKeyLength,
					   uint8_t isCriticalKey, uint8_t keyRef, uint8_t matchLength, uint32_t *getProvider)
{
	uint32_t i;
	uint8_t j;
	KeyDataContainer *KeyDB;
	KeyData *tmpKeyData;

	KeyDB = GetKeyContainer(identifier);
	if(KeyDB == NULL) {
		return 0;
	}

	for(i=0; i<KeyDB->keyCount; i++) {
		if(getProvider == NULL && KeyDB->EmuKeys[i].provider != provider) {
			continue;
		}
		if(strcmp(KeyDB->EmuKeys[i].keyName, keyName)) {
			continue;
		}

		//matchLength cannot be used when multiple keys are allowed
		//for a single provider/keyName combination.
		//Currently this is only the case for Irdeto keys.
		if(matchLength && KeyDB->EmuKeys[i].keyLength != maxKeyLength) {
			continue;
		}

		tmpKeyData = &KeyDB->EmuKeys[i];

		j = 0;
		while(j<keyRef && tmpKeyData->nextKey != NULL) {
			j++;
			tmpKeyData = (KeyData*)tmpKeyData->nextKey;
		}

		if(j == keyRef) {
			memcpy(key, tmpKeyData->key, tmpKeyData->keyLength > maxKeyLength ? maxKeyLength : tmpKeyData->keyLength);
			if(tmpKeyData->keyLength < maxKeyLength) {
				memset(key+tmpKeyData->keyLength, 0, maxKeyLength - tmpKeyData->keyLength);
			}
			if(getProvider != NULL) {
				(*getProvider) = tmpKeyData->provider;
			}
			return 1;
		}
		else {
			break;
		}
	}

	if(isCriticalKey) {
		cs_log("[Emu] Key not found: %c %X %s", identifier, provider, keyName);
	}
	return 0;
}

static int32_t UpdateKey(char identifier, uint32_t provider, char *keyName, uint8_t *key, uint32_t keyLength)
{
	uint8_t *tmpKey = (uint8_t*)malloc(sizeof(uint8_t)*keyLength);
	if(tmpKey == NULL)
	{
		return 0;
	}

	if(FindKey(identifier, provider, keyName, tmpKey, keyLength, 0, 0, 0, NULL)
			&& memcmp(tmpKey, key, keyLength) == 0)
	{
		free(tmpKey);
		return 0;
	}

	free(tmpKey);

	return SetKey(identifier, provider, keyName, key, keyLength, 1);
}


uint8_t read_emu_keyfile(char *opath)
{
	char line[1200], keyName[EMU_MAX_CHAR_KEYNAME], keyString[1026];
	uint32_t pathLength, provider, keyLength;
	uint8_t *key;
	struct dirent *pDirent;
	DIR *pDir;
	char *path, *filepath, filename[EMU_KEY_FILENAME_MAX_LEN+1];
	FILE *file = NULL;
	char identifier;
	uint8_t fileNameLen = strlen(EMU_KEY_FILENAME);

	pathLength = strlen(opath);
	path = (char*)malloc(pathLength+1);
	if(path == NULL) {
		return 0;
	}
	strncpy(path, opath, pathLength+1);

	pathLength = strlen(path);
	if(pathLength >= fileNameLen && strcasecmp(path+pathLength-fileNameLen, EMU_KEY_FILENAME) == 0) {
		// cut file name
		path[pathLength-fileNameLen] = '\0';
	}

	pathLength = strlen(path);
	if(path[pathLength-1] == '/' || path[pathLength-1] == '\\') {
		// cut trailing /
		path[pathLength-1] = '\0';
	}

	pDir = opendir(path);
	if (pDir == NULL) {
		cs_log("cannot open key file path: %s", path);
		free(path);
		return 0;
	}

	while((pDirent = readdir(pDir)) != NULL) {
		if(strcasecmp(pDirent->d_name, EMU_KEY_FILENAME) == 0) {
			strncpy(filename, pDirent->d_name, sizeof(filename));
			break;
		}
	}
	closedir(pDir);

	if(pDirent == NULL) {
		cs_log("key file not found in: %s", path);
		free(path);
		return 0;
	}

	pathLength = strlen(path)+1+strlen(filename)+1;
	filepath = (char*)malloc(pathLength);
	if(filepath == NULL) {
		free(path);
		return 0;
	}
	snprintf(filepath, pathLength, "%s/%s", path, filename);
	free(path);

	cs_log("reading key file: %s", filepath);

	file = fopen(filepath, "r");
	free(filepath);
	if(file == NULL) {
		return 0;
	}

	set_emu_keyfile_path(opath);

	while(fgets(line, 1200, file)) {
		if(sscanf(line, "%c %8x %11s %1024s", &identifier, &provider, keyName, keyString) != 4) {
			continue;
		}

		keyLength = strlen(keyString)/2;
		key = (uint8_t*)malloc(keyLength);
		if(key == NULL) {
			fclose(file);
			return 0;
		}

		CharToBin(key, keyString, strlen(keyString));
		if(!SetKey(identifier, provider, keyName, key, keyLength, 0)) {
			free(key);
		}
	}
	fclose(file);

	return 1;
}

#if !defined(__APPLE__) && !defined(__ANDROID__)
extern uint8_t SoftCamKey_Data[]    __asm__("_binary_SoftCam_Key_start");
extern uint8_t SoftCamKey_DataEnd[] __asm__("_binary_SoftCam_Key_end");

void read_emu_keymemory(void)
{
	char *keyData, *line, *saveptr, keyName[EMU_MAX_CHAR_KEYNAME], keyString[1026];
	uint32_t provider, keyLength;
	uint8_t *key;
	char identifier;

	keyData = (char*)malloc(SoftCamKey_DataEnd-SoftCamKey_Data+1);
	if(keyData == NULL) {
		return;
	}
	memcpy(keyData, SoftCamKey_Data, SoftCamKey_DataEnd-SoftCamKey_Data);
	keyData[SoftCamKey_DataEnd-SoftCamKey_Data] = 0x00;

	line = strtok_r(keyData, "\n", &saveptr);
	while(line != NULL) {
		if(sscanf(line, "%c %8x %11s %1024s", &identifier, &provider, keyName, keyString) != 4) {
			line = strtok_r(NULL, "\n", &saveptr);
			continue;
		}
		keyLength = strlen(keyString)/2;
		key = (uint8_t*)malloc(keyLength);
		if(key == NULL) {
			free(keyData);
			return;
		}

		CharToBin(key, keyString, strlen(keyString));
		if(!SetKey(identifier, provider, keyName, key, keyLength, 0)) {
			free(key);
		}
		line = strtok_r(NULL, "\n", &saveptr);
	}
	free(keyData);
}
#endif

// Shared functions

static inline uint16_t GetEcmLen(const uint8_t *ecm)
{
	return (((ecm[1] & 0x0f)<< 8) | ecm[2]) +3;
}

static void ReverseMem(uint8_t *in, int32_t len)
{
	uint8_t temp;
	int32_t i;
	for(i = 0; i < (len / 2); i++) {
		temp = in[i];
		in[i] = in[len - i - 1];
		in[len - i - 1] = temp;
	}
}

static void ReverseMemInOut(uint8_t *out, const uint8_t *in, int32_t n)
{
	if(n>0) {
		out+=n;
		do {
			*(--out)=*(in++);
		}
		while(--n);
	}
}

static int8_t EmuRSAInput(BIGNUM *d, const uint8_t *in, int32_t n, int8_t le)
{
	int8_t result = 0;

	if(le) {
		uint8_t *tmp = (uint8_t *)malloc(sizeof(uint8_t)*n);
		if(tmp == NULL) {
			return 0;
		}
		ReverseMemInOut(tmp,in,n);
		result = BN_bin2bn(tmp,n,d)!=0;
		free(tmp);
	}
	else {
		result = BN_bin2bn(in,n,d)!=0;
	}
	return result;
}

static int32_t EmuRSAOutput(uint8_t *out, int32_t n, BIGNUM *r, int8_t le)
{
	int32_t s = BN_num_bytes(r);
	if(s>n) {
		uint8_t *buff = (uint8_t *)malloc(sizeof(uint8_t)*s);
		if(buff == NULL) {
			return 0;
		}
		BN_bn2bin(r,buff);
		memcpy(out,buff+s-n,n);
		free(buff);
	}
	else if(s<n) {
		int32_t l=n-s;
		memset(out,0,l);
		BN_bn2bin(r,out+l);
	}
	else {
		BN_bn2bin(r,out);
	}
	if(le) {
		ReverseMem(out,n);
	}
	return s;
}

static int32_t EmuRSA(uint8_t *out, const uint8_t *in, int32_t n, BIGNUM *exp, BIGNUM *mod, int8_t le)
{
	BN_CTX *ctx;
	BIGNUM *r, *d;
	int32_t result = 0;

	ctx = BN_CTX_new();
	r = BN_new();
	d = BN_new();

	if(EmuRSAInput(d,in,n,le) && BN_mod_exp(r,d,exp,mod,ctx)) {
		result = EmuRSAOutput(out,n,r,le);
	}

	BN_free(d);
	BN_free(r);
	BN_CTX_free(ctx);
	return result;
}

static inline void xxor(uint8_t *data, int32_t len, const uint8_t *v1, const uint8_t *v2)
{
	uint32_t i;
	switch(len)
	{
	case 16:
		for(i = 8; i < 16; ++i)
		{
			data[i] = v1[i] ^ v2[i];
		}
	case 8:
		for(i = 4; i < 8; ++i)
		{
			data[i] = v1[i] ^ v2[i];
		}
	case 4:
		for(i = 0; i < 4; ++i)
		{
			data[i] = v1[i] ^ v2[i];
		}
		break;
	default:
		while(len--) { *data++ = *v1++ ^ *v2++; }
		break;
	}
}

static int8_t isValidDCW(uint8_t *dw)
{
	if (((dw[0]+dw[1]+dw[2]) & 0xFF) != dw[3]) {
		return 0;
	}
	if (((dw[4]+dw[5]+dw[6]) & 0xFF) != dw[7]) {
		return 0;
	}
	if (((dw[8]+dw[9]+dw[10]) & 0xFF) != dw[11]) {
		return 0;
	}
	if (((dw[12]+dw[13]+dw[14]) & 0xFF) != dw[15]) {
		return 0;
	}
	return 1;
}

static inline uint8_t GetBit(uint8_t byte, uint8_t bitnb)
{
	return ((byte&(1<<bitnb)) ? 1: 0);
}

static inline uint8_t SetBit(uint8_t val, uint8_t bitnb, uint8_t biton)
{
	return (biton ? (val | (1<<bitnb)) : (val & ~(1<<bitnb)));
}

static void ExpandDesKey(unsigned char *key)
{
	uint8_t i, j, parity;
	uint8_t tmpKey[7];

	memcpy(tmpKey, key, 7);

	key[0] = (tmpKey[0] & 0xFE);
	key[1] = ((tmpKey[0] << 7) | ((tmpKey[1] >> 1) & 0xFE));
	key[2] = ((tmpKey[1] << 6) | ((tmpKey[2] >> 2) & 0xFE));
	key[3] = ((tmpKey[2] << 5) | ((tmpKey[3] >> 3) & 0xFE));
	key[4] = ((tmpKey[3] << 4) | ((tmpKey[4] >> 4) & 0xFE));
	key[5] = ((tmpKey[4] << 3) | ((tmpKey[5] >> 5) & 0xFE));
	key[6] = ((tmpKey[5] << 2) | ((tmpKey[6] >> 6) & 0xFE));
	key[7] = (tmpKey[6] << 1);

	for (i = 0; i < 8; i++)
	{
		parity = 1;
		for (j = 1; j < 8; j++) if ((key[i] >> j) & 0x1) { parity = ~parity & 0x01; }
		key[i] |= parity;
	}
}

// Cryptoworks EMU
static int8_t GetCwKey(uint8_t *buf,uint32_t ident, uint8_t keyIndex, uint32_t keyLength, uint8_t isCriticalKey)
{

	char keyName[EMU_MAX_CHAR_KEYNAME];
	uint32_t tmp;

	if((ident>>4)== 0xD02A) {
		keyIndex &=0xFE;    // map to even number key indexes
	}
	if((ident>>4)== 0xD00C) {
		ident = 0x0D00C0;    // map provider C? to C0
	}
	else if(keyIndex==6 && ((ident>>8) == 0x0D05)) {
		ident = 0x0D0504;    // always use provider 04 system key
	}

	tmp = keyIndex;
	snprintf(keyName, EMU_MAX_CHAR_KEYNAME, "%.2X", tmp);
	if(FindKey('W', ident, keyName, buf, keyLength, isCriticalKey, 0, 0, NULL)) {
		return 1;
	}

	return 0;
}

static const uint8_t cw_sbox1[64] = {
	0xD8,0xD7,0x83,0x3D,0x1C,0x8A,0xF0,0xCF,0x72,0x4C,0x4D,0xF2,0xED,0x33,0x16,0xE0,
	0x8F,0x28,0x7C,0x82,0x62,0x37,0xAF,0x59,0xB7,0xE0,0x00,0x3F,0x09,0x4D,0xF3,0x94,
	0x16,0xA5,0x58,0x83,0xF2,0x4F,0x67,0x30,0x49,0x72,0xBF,0xCD,0xBE,0x98,0x81,0x7F,
	0xA5,0xDA,0xA7,0x7F,0x89,0xC8,0x78,0xA7,0x8C,0x05,0x72,0x84,0x52,0x72,0x4D,0x38
};
static const uint8_t cw_sbox2[64] = {
	0xD8,0x35,0x06,0xAB,0xEC,0x40,0x79,0x34,0x17,0xFE,0xEA,0x47,0xA3,0x8F,0xD5,0x48,
	0x0A,0xBC,0xD5,0x40,0x23,0xD7,0x9F,0xBB,0x7C,0x81,0xA1,0x7A,0x14,0x69,0x6A,0x96,
	0x47,0xDA,0x7B,0xE8,0xA1,0xBF,0x98,0x46,0xB8,0x41,0x45,0x9E,0x5E,0x20,0xB2,0x35,
	0xE4,0x2F,0x9A,0xB5,0xDE,0x01,0x65,0xF8,0x0F,0xB2,0xD2,0x45,0x21,0x4E,0x2D,0xDB
};
static const uint8_t cw_sbox3[64] = {
	0xDB,0x59,0xF4,0xEA,0x95,0x8E,0x25,0xD5,0x26,0xF2,0xDA,0x1A,0x4B,0xA8,0x08,0x25,
	0x46,0x16,0x6B,0xBF,0xAB,0xE0,0xD4,0x1B,0x89,0x05,0x34,0xE5,0x74,0x7B,0xBB,0x44,
	0xA9,0xC6,0x18,0xBD,0xE6,0x01,0x69,0x5A,0x99,0xE0,0x87,0x61,0x56,0x35,0x76,0x8E,
	0xF7,0xE8,0x84,0x13,0x04,0x7B,0x9B,0xA6,0x7A,0x1F,0x6B,0x5C,0xA9,0x86,0x54,0xF9
};
static const uint8_t cw_sbox4[64] = {
	0xBC,0xC1,0x41,0xFE,0x42,0xFB,0x3F,0x10,0xB5,0x1C,0xA6,0xC9,0xCF,0x26,0xD1,0x3F,
	0x02,0x3D,0x19,0x20,0xC1,0xA8,0xBC,0xCF,0x7E,0x92,0x4B,0x67,0xBC,0x47,0x62,0xD0,
	0x60,0x9A,0x9E,0x45,0x79,0x21,0x89,0xA9,0xC3,0x64,0x74,0x9A,0xBC,0xDB,0x43,0x66,
	0xDF,0xE3,0x21,0xBE,0x1E,0x16,0x73,0x5D,0xA2,0xCD,0x8C,0x30,0x67,0x34,0x9C,0xCB
};
static const uint8_t AND_bit1[8] = {0x00,0x40,0x04,0x80,0x21,0x10,0x02,0x08};
static const uint8_t AND_bit2[8] = {0x80,0x08,0x01,0x40,0x04,0x20,0x10,0x02};
static const uint8_t AND_bit3[8] = {0x82,0x40,0x01,0x10,0x00,0x20,0x04,0x08};
static const uint8_t AND_bit4[8] = {0x02,0x10,0x04,0x40,0x80,0x08,0x01,0x20};

static void CW_SWAP_KEY(uint8_t *key)
{
	uint8_t k[8];
	memcpy(k, key, 8);
	memcpy(key, key + 8, 8);
	memcpy(key + 8, k, 8);
}

static void CW_SWAP_DATA(uint8_t *k)
{
	uint8_t d[4];
	memcpy(d, k + 4, 4);
	memcpy(k + 4 ,k ,4);
	memcpy(k, d, 4);
}

static void CW_DES_ROUND(uint8_t *d, uint8_t *k)
{
	uint8_t aa[44] = {1,0,3,1,2,2,3,2,1,3,1,1,3,0,1,2,3,1,3,2,2,0,7,6,5,4,7,6,5,7,6,5,6,7,5,7,5,7,6,6,7,5,4,4};
	uint8_t bb[44] = {0x80,0x08,0x10,0x02,0x08,0x40,0x01,0x20,0x40,0x80,0x04,0x10,0x04,0x01,0x01,0x02,0x20,0x20,0x02,0x01,
					  0x80,0x04,0x02,0x02,0x08,0x02,0x10,0x80,0x01,0x20,0x08,0x80,0x01,0x08,0x40,0x01,0x02,0x80,0x10,0x40,0x40,0x10,0x08,0x01
					 };
	uint8_t ff[4] = {0x02,0x10,0x04,0x04};
	uint8_t l[24] = {0,2,4,6,7,5,3,1,4,5,6,7,7,6,5,4,7,4,5,6,4,7,6,5};

	uint8_t des_td[8], i, o, n, c = 1, m = 0, r = 0, *a = aa, *b = bb, *f = ff, *p1 = l, *p2 = l+8, *p3 = l+16;

	for (m = 0; m < 2; m++) {
		for(i = 0; i < 4; i++) {
			des_td[*p1++] =
				(m) ? ((d[*p2++]*2) & 0x3F) | ((d[*p3++] & 0x80) ? 0x01 : 0x00): (d[*p2++]/2) | ((d[*p3++] & 0x01) ? 0x80 : 0x00);
		}
	}

	for (i = 0; i < 8; i++) {
		c = (c) ? 0 : 1;
		r = (c) ? 6 : 7;
		n = (i) ? i-1 : 1;
		o = (c) ? ((k[n] & *f++) ? 1 : 0) : des_td[n];
		for (m = 1; m < r; m++) {
			o = (c) ? (o*2) | ((k[*a++] & *b++) ? 0x01 : 0x00) : (o/2) | ((k[*a++] & *b++) ? 0x80 : 0x00);
		}
		n = (i) ? n+1 : 0;
		des_td[n] = (c) ? des_td[n] ^ o : (o ^ des_td[n] )/4;
	}

	for( i = 0; i < 8; i++) {
		d[0] ^= (AND_bit1[i] & cw_sbox1[des_td[i]]);
		d[1] ^= (AND_bit2[i] & cw_sbox2[des_td[i]]);
		d[2] ^= (AND_bit3[i] & cw_sbox3[des_td[i]]);
		d[3] ^= (AND_bit4[i] & cw_sbox4[des_td[i]]);
	}

	CW_SWAP_DATA(d);
}

static void CW_48_Key(uint8_t *inkey, uint8_t *outkey, uint8_t algotype)
{
	uint8_t Round_Counter, i = 8, *key128 = inkey, *key48 = inkey + 0x10;
	Round_Counter = 7 - (algotype & 7);

	memset(outkey, 0, 16);
	memcpy(outkey, key48, 6);

	for( ; i > Round_Counter; i--) {
		if (i > 1) {
			outkey[i-2] = key128[i];
		}
	}
}

static void CW_LS_DES_KEY(uint8_t *key,uint8_t Rotate_Counter)
{
	uint8_t round[] = {1,2,2,2,2,2,2,1,2,2,2,2,2,2,1,1};
	uint8_t i, n;
	uint16_t k[8];

	n = round[Rotate_Counter];

	for (i = 0; i < 8; i++) {
		k[i] = key[i];
	}

	for (i = 1; i < n + 1; i++) {
		k[7] = (k[7]*2) | ((k[4] & 0x008) ? 1 : 0);
		k[6] = (k[6]*2) | ((k[7] & 0xF00) ? 1 : 0);
		k[7] &=0xff;
		k[5] = (k[5]*2) | ((k[6] & 0xF00) ? 1 : 0);
		k[6] &=0xff;
		k[4] = ((k[4]*2) | ((k[5] & 0xF00) ? 1 : 0)) & 0xFF;
		k[5] &= 0xff;
		k[3] = (k[3]*2) | ((k[0] & 0x008) ? 1 : 0);
		k[2] = (k[2]*2) | ((k[3] & 0xF00) ? 1 : 0);
		k[3] &= 0xff;
		k[1] = (k[1]*2) | ((k[2] & 0xF00) ? 1 : 0);
		k[2] &= 0xff;
		k[0] = ((k[0]*2) | ((k[1] & 0xF00) ? 1 : 0)) & 0xFF;
		k[1] &= 0xff;
	}
	for (i = 0; i < 8; i++) {
		key[i] = (uint8_t) k[i];
	}
}

static void CW_RS_DES_KEY(uint8_t *k, uint8_t Rotate_Counter)
{
	uint8_t i,c;
	for (i = 1; i < Rotate_Counter+1; i++) {
		c = (k[3] & 0x10) ? 0x80 : 0;
		k[3] /= 2;
		if (k[2] & 1) {
			k[3] |= 0x80;
		}
		k[2] /= 2;
		if (k[1] & 1) {
			k[2] |= 0x80;
		}
		k[1] /= 2;
		if (k[0] & 1) {
			k[1] |= 0x80;
		}
		k[0] /= 2;
		k[0] |= c ;
		c = (k[7] & 0x10) ? 0x80 : 0;
		k[7] /= 2;
		if (k[6] & 1) {
			k[7] |= 0x80;
		}
		k[6] /= 2;
		if (k[5] & 1) {
			k[6] |= 0x80;
		}
		k[5] /= 2;
		if (k[4] & 1) {
			k[5] |= 0x80;
		}
		k[4] /= 2;
		k[4] |= c;
	}
}

static void CW_RS_DES_SUBKEY(uint8_t *k, uint8_t Rotate_Counter)
{
	uint8_t round[] = {1,1,2,2,2,2,2,2,1,2,2,2,2,2,2,1};
	CW_RS_DES_KEY(k, round[Rotate_Counter]);
}

static void CW_PREP_KEY(uint8_t *key )
{
	uint8_t DES_key[8],j;
	int32_t Round_Counter = 6,i,a;
	key[7] = 6;
	memset(DES_key, 0 , 8);
	do {
		a = 7;
		i = key[7];
		j = key[Round_Counter];
		do {
			DES_key[i] = ( (DES_key[i] * 2) | ((j & 1) ? 1: 0) ) & 0xFF;
			j /=2;
			i--;
			if (i < 0) {
				i = 6;
			}
			a--;
		}
		while (a >= 0);
		key[7] = i;
		Round_Counter--;
	}
	while ( Round_Counter >= 0 );
	a = DES_key[4];
	DES_key[4] = DES_key[6];
	DES_key[6] = a;
	DES_key[7] = (DES_key[3] * 16) & 0xFF;
	memcpy(key,DES_key,8);
	CW_RS_DES_KEY(key,4);
}

static void CW_L2DES(uint8_t *data, uint8_t *key, uint8_t algo)
{
	uint8_t i, k0[22], k1[22];
	memcpy(k0,key,22);
	memcpy(k1,key,22);
	CW_48_Key(k0, k1,algo);
	CW_PREP_KEY(k1);
	for (i = 0; i< 2; i++) {
		CW_LS_DES_KEY( k1,15);
		CW_DES_ROUND( data ,k1);
	}
}

static void CW_R2DES(uint8_t *data, uint8_t *key, uint8_t algo)
{
	uint8_t i, k0[22],k1[22];
	memcpy(k0,key,22);
	memcpy(k1,key,22);
	CW_48_Key(k0, k1, algo);
	CW_PREP_KEY(k1);
	for (i = 0; i< 2; i++) {
		CW_LS_DES_KEY(k1,15);
	}
	for (i = 0; i< 2; i++) {
		CW_DES_ROUND( data ,k1);
		CW_RS_DES_SUBKEY(k1,1);
	}
	CW_SWAP_DATA(data);
}

static void CW_DES(uint8_t *data, uint8_t *inkey, uint8_t m)
{
	uint8_t key[22], i;
	memcpy(key, inkey + 9, 8);
	CW_PREP_KEY( key );
	for (i = 16; i > 0; i--) {
		if (m == 1) {
			CW_LS_DES_KEY(key, (uint8_t) (i-1));
		}
		CW_DES_ROUND( data ,key);
		if (m == 0) {
			CW_RS_DES_SUBKEY(key, (uint8_t) (i-1));
		}
	}
}

static void CW_DEC_ENC(uint8_t *d, uint8_t *k, uint8_t a,uint8_t m)
{
	uint8_t n = m & 1;
	CW_L2DES(d , k, a);
	CW_DES (d , k, n);
	CW_R2DES(d , k, a);
	if (m & 2) {
		CW_SWAP_KEY(k);
	}
}

static void Cryptoworks3DES(uint8_t *data, uint8_t *key)
{
	doPC1(key);
	doPC1(&key[8]);
	des(key, DES_ECS2_DECRYPT, data);
	des(&key[8], DES_ECS2_CRYPT, data);
	des(key, DES_ECS2_DECRYPT, data);
}

static uint8_t CryptoworksProcessNano80(uint8_t *data, uint32_t caid, int32_t provider, uint8_t *opKey, uint8_t nanoLength, uint8_t nano80Algo)
{
	int32_t i, j;
	uint8_t key[16], desKey[16], t[8], dat1[8], dat2[8], k0D00C000[16];
	if(nanoLength < 11) {
		return 0;
	}
	if(caid == 0x0D00 && provider != 0xA0 && !GetCwKey(k0D00C000, 0x0D00C0, 0, 16, 1)) {
		return 0;
	}

	if(nano80Algo > 1) {
		return 0;
	}

	memset(t, 0, 8);
	memcpy(dat1, data, 8);

	if(caid == 0x0D00 && provider != 0xA0) {
		memcpy(key, k0D00C000, 16);
	}
	else {
		memcpy(key, opKey, 16);
	}
	Cryptoworks3DES(data, key);
	memcpy(desKey, data, 8);

	memcpy(data, dat1, 8);
	if(caid == 0x0D00 && provider != 0xA0) {
		memcpy(key, &k0D00C000[8], 8);
		memcpy(&key[8], k0D00C000, 8);
	}
	else {
		memcpy(key, &opKey[8], 8);
		memcpy(&key[8], opKey, 8);
	}
	Cryptoworks3DES(data, key);
	memcpy(&desKey[8], data, 8);

	for(i=8; i+7<nanoLength; i+=8) {
		memcpy(dat1, &data[i], 8);
		memcpy(dat2, dat1, 8);
		memcpy(key, desKey, 16);
		Cryptoworks3DES(dat1, key);
		for(j=0; j<8; j++) {
			dat1[j] ^= t[j];
		}
		memcpy(&data[i], dat1, 8);
		memcpy(t, dat2, 8);
	}

	return data[10] + 5;
}

static void CryptoworksSignature(const uint8_t *data, uint32_t length, uint8_t *key, uint8_t *signature)
{
	uint32_t i, sigPos;
	int8_t algo, first;

	algo = data[0] & 7;
	if(algo == 7) {
		algo = 6;
	}
	memset(signature, 0, 8);
	first = 1;
	sigPos = 0;
	for(i=0; i<length; i++) {
		signature[sigPos] ^= data[i];
		sigPos++;

		if(sigPos > 7) {
			if (first) {
				CW_L2DES(signature, key, algo);
			}
			CW_DES(signature, key, 1);

			sigPos = 0;
			first = 0;
		}
	}
	if(sigPos > 0) {
		CW_DES(signature, key, 1);
	}
	CW_R2DES(signature, key, algo);
}

static void CryptoworksDecryptDes(uint8_t *data, uint8_t algo, uint8_t *key)
{
	int32_t i;
	uint8_t k[22], t[8];

	algo &= 7;
	if(algo<7) {
		CW_DEC_ENC(data, key, algo, DES_RIGHT);
	}
	else {
		memcpy(k, key, 22);
		for(i=0; i<3; i++) {
			CW_DEC_ENC(data, k, algo, i&1);
			memcpy(t,k,8);
			memcpy(k,k+8,8);
			memcpy(k+8,t,8);
		}
	}
}

static int8_t CryptoworksECM(uint32_t caid, uint8_t *ecm, uint8_t *cw)
{
	uint32_t ident;
	uint8_t keyIndex = 0, nanoLength, newEcmLength, key[22], signature[8], nano80Algo = 1;
	int32_t provider = -1;
	uint16_t i, j, ecmLen = GetEcmLen(ecm);

	if(ecmLen < 8) {
		return 1;
	}
	if(ecm[7] != ecmLen - 8) {
		return 1;
	}

	memset(key, 0, 22);

	for(i = 8; i+1 < ecmLen; i += ecm[i+1] + 2) {
		if(ecm[i] == 0x83 && i+2 < ecmLen) {
			provider = ecm[i+2] & 0xFC;
			keyIndex = ecm[i+2] & 3;
			keyIndex = keyIndex ? 1 : 0;
		}
		else if(ecm[i] == 0x84 && i+3 < ecmLen) {
			//nano80Provider = ecm[i+2] & 0xFC;
			//nano80KeyIndex = ecm[i+2] & 3;
			//nano80KeyIndex = nano80KeyIndex ? 1 : 0;
			nano80Algo = ecm[i+3];
		}
	}

	if(provider < 0) {
		switch(caid) {
		case 0x0D00:
			provider = 0xC0;
			break;
		case 0x0D02:
			provider = 0xA0;
			break;
		case 0x0D03:
			provider = 0x04;
			break;
		case 0x0D05:
			provider = 0x04;
			break;
		default:
			return 1;
		}
	}

	ident = (caid << 8) | provider;
	if(!GetCwKey(key, ident, keyIndex, 16, 1)) {
		return 2;
	}
	if(!GetCwKey(&key[16], ident, 6, 6, 1)) {
		return 2;
	}

	for(i = 8; i+1 < ecmLen; i += ecm[i+1] + 2) {
		if(ecm[i] == 0x80 && i+2+7 < ecmLen && i+2+ecm[i+1] <= ecmLen
				&& (provider == 0xA0 || provider == 0xC0 || provider == 0xC4 || provider == 0xC8)) {
			nanoLength = ecm[i+1];
			newEcmLength = CryptoworksProcessNano80(ecm+i+2, caid, provider, key, nanoLength, nano80Algo);
			if(newEcmLength == 0 || newEcmLength > ecmLen-(i+2+3)) {
				return 1;
			}
			ecm[i+2+3] = 0x81;
			ecm[i+2+4] = 0x70;
			ecm[i+2+5] = newEcmLength;
			ecm[i+2+6] = 0x81;
			ecm[i+2+7] = 0xFF;
			return CryptoworksECM(caid, ecm+i+2+3, cw);
		}
	}

	if(ecmLen - 15 < 1) {
		return 1;
	}
	CryptoworksSignature(ecm + 5, ecmLen - 15, key, signature);
	for(i = 8; i+1 < ecmLen; i += ecm[i+1]+2) {
		switch(ecm[i]) {
		case 0xDA:
		case 0xDB:
		case 0xDC:
			if(i+2+ecm[i+1] > ecmLen) {
				break;
			}
			for(j=0; j+7<ecm[i+1]; j+=8) {
				CryptoworksDecryptDes(&ecm[i+2+j], ecm[5], key);
			}
			break;
		case 0xDF:
			if(i+2+8 > ecmLen) {
				break;
			}
			if(memcmp(&ecm[i+2], signature, 8)) {
				return 6;
			}
			break;
		}
	}

	for(i = 8; i+1 < ecmLen; i += ecm[i+1]+2) {
		switch(ecm[i]) {
		case 0xDB:
			if(i+2+ecm[i+1] <= ecmLen && ecm[i+1]==16) {
				memcpy(cw, &ecm[i+2], 16);
				return 0;
			}
			break;
		}
	}

	return 5;
}

// SoftNDS EMU
static const uint8_t nds_const[]= {0x0F,0x1E,0x2D,0x3C,0x4B,0x5A,0x69,0x78,0x87,0x96,0xA5,0xB4,0xC3,0xD2,0xE1,0xF0};

static uint8_t viasat_const[]= {
	0x15,0x85,0xC5,0xE4,0xB8,0x52,0xEC,0xF7,0xC3,0xD9,0x08,0xBA,0x22,0x4A,0x66,0xF2,
	0x82,0x15,0x4F,0xB2,0x18,0x48,0x63,0x97,0xDC,0x19,0xD8,0x51,0x9A,0x39,0xFC,0xCA,
	0x1C,0x24,0xD0,0x65,0xA9,0x66,0x2D,0xD6,0x53,0x3B,0x86,0xBA,0x40,0xEA,0x4C,0x6D,
	0xD9,0x1E,0x41,0x14,0xFE,0x15,0xAF,0xC3,0x18,0xC5,0xF8,0xA7,0xA8,0x01,0x00,0x01,
};

static int8_t SoftNDSECM(uint16_t caid, uint8_t *ecm, uint8_t *dw)
{
	int32_t i;
	uint8_t *tDW, irdEcmLen, offsetCw = 0, offsetP2 = 0;
	uint8_t digest[16], md5_const[64];
	MD5_CTX mdContext;
	uint16_t ecmLen = GetEcmLen(ecm);

	if(ecmLen < 7) {
		return 1;
	}

	if(ecm[3] != 0x00 || ecm[4] != 0x00 || ecm[5] != 0x01) {
		return 1;
	}

	irdEcmLen = ecm[6];
	if(irdEcmLen < (10+3+8+4) || irdEcmLen+6 >= ecmLen) {
		return 1;
	}

	for(i=0; 10+i+2 < irdEcmLen; i++) {
		if(ecm[17+i] == 0x0F && ecm[17+i+1] == 0x40 && ecm[17+i+2] == 0x00) {
			offsetCw = 17+i+3;
			offsetP2 = offsetCw+9;
		}
	}

	if(offsetCw == 0 || offsetP2 == 0) {
		return 1;
	}

	if(offsetP2-7+4 > irdEcmLen) {
		return 1;
	}

	if(caid == 0x090F || caid == 0x093E) {
		memcpy(md5_const, viasat_const, 64);
	}
	else if(!FindKey('S', caid, "00", md5_const, 64, 1, 0, 0, NULL)) {
		return 2;
	}

	memset(dw,0,16);
	tDW = &dw[ecm[0]==0x81 ? 8 : 0];

	MD5_Init(&mdContext);
	MD5_Update(&mdContext, ecm+7, 10);
	MD5_Update(&mdContext, ecm+offsetP2, 4);
	MD5_Update(&mdContext, md5_const, 64);
	MD5_Update(&mdContext, nds_const, 16);
	MD5_Final(digest, &mdContext);

	for (i=0; i<8; i++) {
		tDW[i] = digest[i+8] ^ ecm[offsetCw+i];
	}

	if(((tDW[0]+tDW[1]+tDW[2])&0xFF)-tDW[3]) {
		return 6;
	}
	if(((tDW[4]+tDW[5]+tDW[6])&0xFF)-tDW[7]) {
		return 6;
	}

	return 0;
}

// Viaccess EMU
static int8_t GetViaKey(uint8_t *buf, uint32_t ident, char keyName, uint32_t keyIndex, uint32_t keyLength, uint8_t isCriticalKey)
{

	char keyStr[EMU_MAX_CHAR_KEYNAME];
	snprintf(keyStr, EMU_MAX_CHAR_KEYNAME, "%c%X", keyName, keyIndex);
	if(FindKey('V', ident, keyStr, buf, keyLength, isCriticalKey, 0, 0, NULL)) {
		return 1;
	}

	if(ident == 0xD00040 && FindKey('V', 0x030B00, keyStr, buf, keyLength, isCriticalKey, 0, 0, NULL)) {
		return 1;
	}

	return 0;
}

static void Via1Mod(const uint8_t* key2, uint8_t* data)
{
	int32_t kb, db;
	for (db=7; db>=0; db--) {
		for (kb=7; kb>3; kb--) {
			int32_t a0=kb^db;
			int32_t pos=7;
			if (a0&4) {
				a0^=7;
				pos^=7;
			}
			a0=(a0^(kb&3)) + (kb&3);
			if (!(a0&4)) {
				data[db]^=(key2[kb] ^ ((data[kb^pos]*key2[kb^4]) & 0xFF));
			}
		}
	}
	for (db=0; db<8; db++) {
		for (kb=0; kb<4; kb++) {
			int32_t a0=kb^db;
			int32_t pos=7;
			if (a0&4) {
				a0^=7;
				pos^=7;
			}
			a0=(a0^(kb&3)) + (kb&3);
			if (!(a0&4)) {
				data[db]^=(key2[kb] ^ ((data[kb^pos]*key2[kb^4]) & 0xFF));
			}
		}
	}
}

static void Via1Decode(uint8_t *data, uint8_t *key)
{
	Via1Mod(key+8, data);
	des(key, DES_ECM_CRYPT, data);
	Via1Mod(key+8, data);
}

static void Via1Hash(uint8_t *data, uint8_t *key)
{
	Via1Mod(key+8, data);
	des(key, DES_ECM_HASH, data);
	Via1Mod(key+8, data);
}

static inline void Via1DoHash(uint8_t *hashbuffer, uint8_t *pH, uint8_t data, uint8_t *hashkey)
{
	hashbuffer[*pH] ^= data;
	(*pH)++;

	if(*pH == 8) {
		Via1Hash(hashbuffer, hashkey);
		*pH = 0;
	}
}

static int8_t Via1Decrypt(uint8_t* ecm, uint8_t* dw, uint32_t ident, uint8_t desKeyIndex)
{
	uint8_t work_key[16];
	uint8_t *data, *des_data1, *des_data2;
	uint16_t ecmLen = GetEcmLen(ecm);
	int32_t msg_pos;
	int32_t encStart = 0, hash_start, i;
	uint8_t signature[8], hashbuffer[8], prepared_key[16], hashkey[16];
	uint8_t tmp, k, pH, foundData = 0;

	if (ident == 0) {
		return 4;
	}
	memset(work_key, 0, 16);
	if(!GetViaKey(work_key, ident, '0', desKeyIndex, 8, 1)) {
		return 2;
	}

	if(ecmLen < 11) {
		return 1;
	}
	data = ecm+9;
	des_data1 = dw;
	des_data2 = dw+8;

	msg_pos = 0;
	pH = 0;
	memset(hashbuffer, 0, sizeof(hashbuffer));
	memcpy(hashkey, work_key, sizeof(hashkey));
	memset(signature, 0, 8);

	while(9+msg_pos+2 < ecmLen) {
		switch (data[msg_pos]) {
		case 0xea:
			if(9+msg_pos+2+15 < ecmLen) {
				encStart = msg_pos + 2;
				memcpy(des_data1, &data[msg_pos+2], 8);
				memcpy(des_data2, &data[msg_pos+2+8], 8);
				foundData |= 1;
			}
			break;
		case 0xf0:
			if(9+msg_pos+2+7 < ecmLen) {
				memcpy(signature, &data[msg_pos+2], 8);
				foundData |= 2;
			}
			break;
		}
		msg_pos += data[msg_pos+1]+2;
	}

	if(foundData != 3) {
		return 1;
	}

	pH=i=0;

	if(data[0] == 0x9f && 10+data[1] <= ecmLen) {
		Via1DoHash(hashbuffer, &pH, data[i++], hashkey);
		Via1DoHash(hashbuffer, &pH, data[i++], hashkey);

		for (hash_start=0; hash_start < data[1]; hash_start++) {
			Via1DoHash(hashbuffer, &pH, data[i++], hashkey);
		}

		while (pH != 0) {
			Via1DoHash(hashbuffer, &pH, 0, hashkey);
		}
	}

	if (work_key[7] == 0) {
		for (; i < encStart + 16; i++) {
			Via1DoHash(hashbuffer, &pH, data[i], hashkey);
		}
		memcpy(prepared_key, work_key, 8);
	}
	else {
		prepared_key[0] = work_key[2];
		prepared_key[1] = work_key[3];
		prepared_key[2] = work_key[4];
		prepared_key[3] = work_key[5];
		prepared_key[4] = work_key[6];
		prepared_key[5] = work_key[0];
		prepared_key[6] = work_key[1];
		prepared_key[7] = work_key[7];
		memcpy(prepared_key+8, work_key+8, 8);

		if (work_key[7] & 1) {
			for (; i < encStart; i++) {
				Via1DoHash(hashbuffer, &pH, data[i], hashkey);
			}

			k = ((work_key[7] & 0xf0) == 0) ? 0x5a : 0xa5;

			for (i=0; i<8; i++) {
				tmp = des_data1[i];
				des_data1[i] = (k & hashbuffer[pH] ) ^ tmp;
				Via1DoHash(hashbuffer, &pH, tmp, hashkey);
			}

			for (i = 0; i < 8; i++) {
				tmp = des_data2[i];
				des_data2[i] = (k & hashbuffer[pH] ) ^ tmp;
				Via1DoHash(hashbuffer, &pH, tmp, hashkey);
			}
		}
		else {
			for (; i < encStart + 16; i++) {
				Via1DoHash(hashbuffer, &pH, data[i], hashkey);
			}
		}
	}
	Via1Decode(des_data1, prepared_key);
	Via1Decode(des_data2, prepared_key);
	Via1Hash(hashbuffer, hashkey);
	if(memcmp(signature, hashbuffer, 8)) {
		return 6;
	}
	return 0;
}

static int8_t Via26ProcessDw(uint8_t *indata, uint32_t ident, uint8_t desKeyIndex)
{
	uint8_t pv1,pv2, i;
	uint8_t Tmp[8], tmpKey[16], T1Key[300], P1Key[8], KeyDes1[16], KeyDes2[16], XorKey[8];

	if(!GetViaKey(T1Key, ident, 'T', 1, 300, 1)) {
		return 2;
	}
	if(!GetViaKey(P1Key, ident, 'P', 1, 8, 1)) {
		return 2;
	}
	if(!GetViaKey(KeyDes1, ident, 'D', 1, 16, 1)) {
		return 2;
	}
	if(!GetViaKey(KeyDes2, ident, '0', desKeyIndex, 16, 1)) {
		return 2;
	}
	if(!GetViaKey(XorKey, ident, 'X', 1, 8, 1)) {
		return 2;
	}

	for (i=0; i<8; i++) {
		pv1 = indata[i];
		Tmp[i] = T1Key[pv1];
	}
	for (i=0; i<8; i++) {
		pv1 = P1Key[i];
		pv2 = Tmp[pv1];
		indata[i]=pv2;
	}
	memcpy(tmpKey, KeyDes1,8);
	doPC1(tmpKey) ;
	des(tmpKey, DES_ECS2_CRYPT,indata);
	for (i=0; i<8; i++) {
		indata[i] ^= XorKey[i];
	}
	memcpy(tmpKey,KeyDes2,16);
	doPC1(tmpKey);
	doPC1(tmpKey+8);
	des(tmpKey,DES_ECS2_DECRYPT,indata);
	des(tmpKey+8, DES_ECS2_CRYPT,indata);
	des(tmpKey,DES_ECS2_DECRYPT,indata);
	for (i=0; i<8; i++) {
		indata[i] ^= XorKey[i];
	}
	memcpy(tmpKey, KeyDes1,8);
	doPC1(tmpKey);
	des(tmpKey,DES_ECS2_DECRYPT,indata);

	for (i=0; i<8; i++) {
		pv1 = indata[i];
		pv2 = P1Key[i];
		Tmp[pv2] = pv1;
	}
	for (i=0; i<8; i++) {
		pv1 =  Tmp[i];
		pv2 =  T1Key[pv1];
		indata[i] = pv2;
	}
	return 0;
}

static int8_t Via26Decrypt(uint8_t* source, uint8_t* dw, uint32_t ident, uint8_t desKeyIndex)
{
	uint8_t tmpData[8], C1[8];
	uint8_t *pXorVector;
	int32_t i,j;

	if (ident == 0) {
		return 4;
	}
	if(!GetViaKey(C1, ident, 'C', 1, 8, 1)) {
		return 2;
	}

	for (i=0; i<2; i++) {
		memcpy(tmpData, source+ i*8, 8);
		Via26ProcessDw(tmpData, ident, desKeyIndex);
		if (i!=0) {
			pXorVector = source;
		}
		else {
			pXorVector = &C1[0];
		}
		for (j=0; j<8; j++) {
			dw[i*8+j] = tmpData[j]^pXorVector[j];
		}
	}
	return 0;
}

static void Via3Core(uint8_t *data, uint8_t Off, uint32_t ident, uint8_t* XorKey, uint8_t* T1Key)
{
	uint8_t i;
	uint32_t lR2, lR3, lR4, lR6, lR7;

	switch (ident) {
	case 0x032820: {
		for (i=0; i<4; i++) {
			data[i]^= XorKey[(Off+i) & 0x07];
		}
		lR2 = (data[0]^0xBD)+data[0];
		lR3 = (data[3]^0xEB)+data[3];
		lR2 = (lR2-lR3)^data[2];
		lR3 = ((0x39*data[1])<<2);
		data[4] = (lR2|lR3)+data[2];
		lR3 = ((((data[0]+6)^data[0]) | (data[2]<<1))^0x65)+data[0];
		lR2 = (data[1]^0xED)+data[1];
		lR7 = ((data[3]+0x29)^data[3])*lR2;
		data[5] = lR7+lR3;
		lR2 = ((data[2]^0x33)+data[2]) & 0x0A;
		lR3 = (data[0]+0xAD)^data[0];
		lR3 = lR3+lR2;
		lR2 = data[3]*data[3];
		lR7 = (lR2 | 1) + data[1];
		data[6] = (lR3|lR7)+data[1];
		lR3 = data[1] & 0x07;
		lR2 = (lR3-data[2]) & (data[0] | lR2 |0x01);
		data[7] = lR2+data[3];
		for (i=0; i<4; i++) {
			data[i+4] = T1Key[data[i+4]];
		}
	}
	break;
	case 0x030B00: {
		for (i=0; i<4; i++) {
			data[i]^= XorKey[(Off+i) & 0x07];
		}
		lR6 = (data[3] + 0x6E) ^ data[3];
		lR6 = (lR6*(data[2] << 1)) + 0x17;
		lR3 = (data[1] + 0x77) ^ data[1];
		lR4 = (data[0] + 0xD7) ^ data[0];
		data[4] = ((lR4 & lR3) | lR6) + data[0];
		lR4 = ((data[3] + 0x71) ^ data[3]) ^ 0x90;
		lR6 = (data[1] + 0x1B) ^ data[1];
		lR4 = (lR4*lR6) ^ data[0];
		data[5] = (lR4 ^ (data[2] << 1)) + data[1];
		lR3 = (data[3] * data[3])| 0x01;
		lR4 = (((data[2] ^ 0x35) + data[2]) | lR3) + data[2];
		lR6 = data[1] ^ (data[0] + 0x4A);
		data[6] = lR6 + lR4;
		lR3 = (data[0] * (data[2] << 1)) | data[1];
		lR4 = 0xFE - data[3];
		lR3 = lR4 ^ lR3;
		data[7] = lR3 + data[3];
		for (i=0; i<4; i++) {
			data[4+i] = T1Key[data[4+i]];
		}
	}
	break;
	default:
		break;
	}
}

static void Via3Fct1(uint8_t *data, uint32_t ident, uint8_t* XorKey, uint8_t* T1Key)
{
	uint8_t t;
	Via3Core(data, 0, ident, XorKey, T1Key);

	switch (ident) {
	case 0x032820: {
		t = data[4];
		data[4] = data[7];
		data[7] = t;
	}
	break;
	case 0x030B00: {
		t = data[5];
		data[5] = data[7];
		data[7] = t;
	}
	break;
	default:
		break;
	}
}

static void Via3Fct2(uint8_t *data, uint32_t ident, uint8_t* XorKey, uint8_t* T1Key)
{
	uint8_t t;
	Via3Core(data, 4, ident, XorKey, T1Key);

	switch (ident) {
	case 0x032820: {
		t = data[4];
		data[4] = data[7];
		data[7] = data[5];
		data[5] = data[6];
		data[6] = t;
	}
	break;
	case 0x030B00: {
		t = data[6];
		data[6] = data[7];
		data[7] = t;
	}
	break;
	default:
		break;
	}
}

static int8_t Via3ProcessDw(uint8_t *data, uint32_t ident, uint8_t desKeyIndex)
{
	uint8_t i;
	uint8_t tmp[8], tmpKey[16], T1Key[300], P1Key[8], KeyDes[16], XorKey[8];

	if(!GetViaKey(T1Key, ident, 'T', 1, 300, 1)) {
		return 2;
	}
	if(!GetViaKey(P1Key, ident, 'P', 1, 8, 1)) {
		return 2;
	}
	if(!GetViaKey(KeyDes, ident, '0', desKeyIndex, 16, 1)) {
		return 2;
	}
	if(!GetViaKey(XorKey, ident, 'X', 1, 8, 1)) {
		return 2;
	}

	for (i=0; i<4; i++) {
		tmp[i] = data[i+4];
	}
	Via3Fct1(tmp, ident, XorKey, T1Key);
	for (i=0; i<4; i++) {
		tmp[i] = data[i]^tmp[i+4];
	}
	Via3Fct2(tmp, ident, XorKey, T1Key);
	for (i=0; i<4; i++) {
		tmp[i]^= XorKey[i+4];
	}
	for (i=0; i<4; i++) {
		data[i] = data[i+4]^tmp[i+4];
		data[i+4] = tmp[i];
	}
	memcpy(tmpKey,KeyDes,16);
	doPC1(tmpKey);
	doPC1(tmpKey+8);
	des(tmpKey, DES_ECS2_DECRYPT, data);
	des(tmpKey+8, DES_ECS2_CRYPT, data);
	des(tmpKey, DES_ECS2_DECRYPT, data);
	for (i=0; i<4; i++) {
		tmp[i] = data[i+4];
	}
	Via3Fct2(tmp, ident, XorKey, T1Key);
	for (i=0; i<4; i++) {
		tmp[i] = data[i]^tmp[i+4];
	}
	Via3Fct1(tmp, ident, XorKey, T1Key);
	for (i=0; i<4; i++) {
		tmp[i]^= XorKey[i];
	}
	for (i=0; i<4; i++) {
		data[i] = data[i+4]^tmp[i+4];
		data[i+4] = tmp[i];
	}
	return 0;
}

static void Via3FinalMix(uint8_t *dw)
{
	uint8_t tmp[4];

	memcpy(tmp, dw, 4);
	memcpy(dw, dw + 4, 4);
	memcpy(dw + 4, tmp, 4);

	memcpy(tmp, dw + 8, 4);
	memcpy(dw + 8, dw + 12, 4);
	memcpy(dw + 12, tmp, 4);
}

static int8_t Via3Decrypt(uint8_t* source, uint8_t* dw, uint32_t ident, uint8_t desKeyIndex, uint8_t aesKeyIndex, uint8_t aesMode, int8_t doFinalMix)
{
	int8_t aesAfterCore = 0;
	int8_t needsAES = (aesKeyIndex != 0xFF);
	uint8_t tmpData[8], C1[8];
	uint8_t *pXorVector;
	char aesKey[16];
	int32_t i, j;

	if(ident == 0) {
		return 4;
	}
	if(!GetViaKey(C1, ident, 'C', 1, 8, 1)) {
		return 2;
	}
	if(needsAES && !GetViaKey((uint8_t*)aesKey, ident, 'E', aesKeyIndex, 16, 1)) {
		return 2;
	}
	if(aesMode==0x0D || aesMode==0x11 || aesMode==0x15) {
		aesAfterCore = 1;
	}

	if(needsAES && !aesAfterCore) {
		if(aesMode == 0x0F) {
			hdSurEncPhase1_D2_0F_11(source);
			hdSurEncPhase2_D2_0F_11(source);
		}
		else if(aesMode == 0x13) {
			hdSurEncPhase1_D2_13_15(source);
		}
		struct aes_keys aes;
		aes_set_key(&aes, aesKey);
		aes_decrypt(&aes, source, 16);
		if(aesMode == 0x0F) {
			hdSurEncPhase1_D2_0F_11(source);
		}
		else if(aesMode == 0x13) {
			hdSurEncPhase2_D2_13_15(source);
		}
	}

	for(i=0; i<2; i++) {
		memcpy(tmpData, source+i*8, 8);
		Via3ProcessDw(tmpData, ident, desKeyIndex);
		if (i!=0) {
			pXorVector = source;
		}
		else {
			pXorVector = &C1[0];
		}
		for (j=0; j<8; j++) {
			dw[i*8+j] = tmpData[j]^pXorVector[j];
		}
	}

	if(needsAES && aesAfterCore) {
		if(aesMode == 0x11) {
			hdSurEncPhase1_D2_0F_11(dw);
			hdSurEncPhase2_D2_0F_11(dw);
		}
		else if(aesMode == 0x15) {
			hdSurEncPhase1_D2_13_15(dw);
		}
		struct aes_keys aes;
		aes_set_key(&aes, aesKey);
		aes_decrypt(&aes, dw, 16);
		if(aesMode == 0x11) {
			hdSurEncPhase1_D2_0F_11(dw);
		}
		if(aesMode == 0x15) {
			hdSurEncPhase2_D2_13_15(dw);
		}
	}

	if(ident == 0x030B00) {
		if(doFinalMix) {
			Via3FinalMix(dw);
		}
		if(!isValidDCW(dw)) {
			return 6;
		}
	}
	return 0;
}

static int8_t ViaccessECM(uint8_t *ecm, uint8_t *dw)
{
	uint32_t currentIdent = 0;
	uint8_t nanoCmd = 0, nanoLen = 0, version = 0, providerKeyLen = 0, desKeyIndex = 0, aesMode = 0, aesKeyIndex = 0xFF;
	int8_t doFinalMix = 0, result = 1;
	uint16_t i = 0, keySelectPos = 0, ecmLen = GetEcmLen(ecm);

	for (i=4; i+2<ecmLen; ) {
		nanoCmd = ecm[i++];
		nanoLen = ecm[i++];
		if(i+nanoLen > ecmLen) {
			return 1;
		}

		switch (nanoCmd) {
		case 0x40:
			if (nanoLen < 0x03) {
				break;
			}
			version = ecm[i];
			if (nanoLen == 3) {
				currentIdent=((ecm[i]<<16)|(ecm[i+1]<<8))|(ecm[i+2]&0xF0);
				desKeyIndex = ecm[i+2]&0x0F;
				keySelectPos = i+3;
			}
			else {
				currentIdent =(ecm[i]<<16)|(ecm[i+1]<<8)|((ecm[i+2]>>4)&0x0F);
				desKeyIndex = ecm[i+3];
				keySelectPos = i+4;
			}
			providerKeyLen = nanoLen;
			break;
		case 0x90:
			if (nanoLen < 0x03) {
				break;
			}
			version = ecm[i];
			currentIdent= ((ecm[i]<<16)|(ecm[i+1]<<8))|(ecm[i+2]&0xF0);
			desKeyIndex = ecm[i+2]&0x0F;
			keySelectPos = i+4;
			if((version == 3) && (nanoLen > 3)) {
				desKeyIndex = ecm[i+(nanoLen-4)]&0x0F;
			}
			providerKeyLen = nanoLen;
			break;
		case 0x80:
			nanoLen = 0;
			break;
		case 0xD2:
			if (nanoLen < 0x02) {
				break;
			}
			aesMode = ecm[i];
			aesKeyIndex = ecm[i+1];
			break;
		case 0xDD:
			nanoLen = 0;
			break;
		case 0xEA:
			if (nanoLen < 0x10) {
				break;
			}

			if (version < 2) {
				return Via1Decrypt(ecm, dw, currentIdent, desKeyIndex);
			}
			else if (version == 2) {
				return Via26Decrypt(ecm + i, dw, currentIdent, desKeyIndex);
			}
			else if (version == 3) {
				doFinalMix = 0;
				if (currentIdent == 0x030B00 && providerKeyLen>3) {
					if(keySelectPos+2 >= ecmLen) {
						break;
					}
					if (ecm[keySelectPos]==0x05 && ecm[keySelectPos+1]==0x67 && (ecm[keySelectPos+2]==0x00 || ecm[keySelectPos+2]==0x01)) {
						if(ecm[keySelectPos+2]==0x01) {
							doFinalMix = 1;
						}
					}
					else {
						break;
					}
				}
				return Via3Decrypt(ecm + i, dw, currentIdent, desKeyIndex, aesKeyIndex, aesMode, doFinalMix);
			}
			break;
		default:
			break;
		}
		i += nanoLen;
	}
	return result;
}

// Nagra EMU
static int8_t GetNagraKey(uint8_t *buf, uint32_t ident, char keyName, uint32_t keyIndex, uint8_t isCriticalKey)
{
	char keyStr[EMU_MAX_CHAR_KEYNAME];
	snprintf(keyStr, EMU_MAX_CHAR_KEYNAME, "%c%X", keyName, keyIndex);
	if(FindKey('N', ident, keyStr, buf, keyName == 'M' ? 64 : 16, isCriticalKey, 0, 0, NULL)) {
		return 1;
	}

	return 0;
}

static int8_t Nagra2Signature(const uint8_t *vkey, const uint8_t *sig, const uint8_t *msg, int32_t len)
{
	uint8_t buff[16];
	uint8_t iv[8];
	int32_t i,j;

	memcpy(buff,vkey,sizeof(buff));
	for(i=0; i+7<len; i+=8) {
		IDEA_KEY_SCHEDULE ek;
		idea_set_encrypt_key(buff, &ek);
		memcpy(buff,buff+8,8);
		memset(iv,0,sizeof(iv));
		idea_cbc_encrypt(msg+i,buff+8,8,&ek,iv,IDEA_ENCRYPT);
		for(j=7; j>=0; j--) {
			buff[j+8]^=msg[i+j];
		}
	}
	buff[8]&=0x7F;
	return (memcmp(sig,buff+8,8)==0);
}

static int8_t DecryptNagra2ECM(uint8_t *in, uint8_t *out, const uint8_t *key, int32_t len, const uint8_t *vkey, uint8_t *keyM)
{
	BIGNUM *exp, *mod;
	uint8_t iv[8];
	int32_t i = 0, sign = in[0] & 0x80;
	uint8_t binExp = 3;
	int8_t result = 1;

	exp = BN_new();
	mod = BN_new();
	BN_bin2bn(&binExp, 1, exp);
	BN_bin2bn(keyM, 64, mod);

	if(EmuRSA(out,in+1,64,exp,mod,1)<=0) {
		BN_free(exp);
		BN_free(mod);
		return 0;
	}
	out[63]|=sign;
	if(len>64) {
		memcpy(out+64,in+65,len-64);
	}

	memset(iv,0,sizeof(iv));
	if(in[0]&0x04) {
		uint8_t key1[8], key2[8];
		ReverseMemInOut(key1,&key[0],8);
		ReverseMemInOut(key2,&key[8],8);

		for(i=7; i>=0; i--) {
			ReverseMem(out+8*i,8);
		}
		des_ede2_cbc_decrypt(out, iv, key1, key2, len);
		for(i=7; i>=0; i--) {
			ReverseMem(out+8*i,8);
		}
	}
	else {
		IDEA_KEY_SCHEDULE ek;
		idea_set_encrypt_key(key, &ek);
		idea_cbc_encrypt(out, out, len&~7, &ek, iv, IDEA_DECRYPT);
	}

	ReverseMem(out,64);
	if(result && EmuRSA(out,out,64,exp,mod,0)<=0) {
		result = 0;
	}
	if(result && vkey && !Nagra2Signature(vkey,out,out+8,len-8)) {
		result = 0;
	}

	BN_free(exp);
	BN_free(mod);
	return result;
}

static int8_t Nagra2ECM(uint8_t *ecm, uint8_t *dw)
{
	uint32_t ident, identMask, tmp1, tmp2, tmp3;
	uint8_t cmdLen, ideaKeyNr, *dec, ideaKey[16], vKey[16], m1Key[64], mecmAlgo = 0;
	int8_t useVerifyKey = 0;
	int32_t l=0, s;
	uint16_t i = 0, ecmLen = GetEcmLen(ecm);

	if(ecmLen < 8) {
		return 1;
	}
	cmdLen = ecm[4] - 5;
	ident = (ecm[5] << 8) + ecm[6];
	ideaKeyNr = (ecm[7]&0x10)>>4;
	if(ideaKeyNr) {
		ideaKeyNr = 1;
	}
	if(ident == 1283 || ident == 1285 || ident == 1297) {
		ident = 1281;
	}
	if(cmdLen <= 63 || ecmLen < cmdLen + 10) {
		return 1;
	}

	if(!GetNagraKey(ideaKey, ident, '0', ideaKeyNr, 1)) {
		return 2;
	}
	if(GetNagraKey(vKey, ident, 'V', 0, 0)) {
		useVerifyKey = 1;
	}
	if(!GetNagraKey(m1Key, ident, 'M', 1, 1)) {
		return 2;
	}
	ReverseMem(m1Key, 64);

	dec = (uint8_t*)malloc(sizeof(uint8_t)*cmdLen);
	if(dec == NULL) {
		return 7;
	}
	if(!DecryptNagra2ECM(ecm+9, dec, ideaKey, cmdLen, useVerifyKey?vKey:0, m1Key)) {
		free(dec);
		return 1;
	}

	for(i=(dec[14]&0x10)?16:20; i<cmdLen && l!=3; ) {
		switch(dec[i]) {
		case 0x10:
		case 0x11:
			if(i+10 < cmdLen && dec[i+1]==0x09) {
				s = (~dec[i])&1;
				mecmAlgo = dec[i+2]&0x60;
				memcpy(dw+(s<<3),&dec[i+3],8);
				i+=11;
				l|=(s+1);
			}
			else {
				i++;
			}
			break;
		case 0x00:
			i+=2;
			break;
		case 0x30:
		case 0x31:
		case 0x32:
		case 0x33:
		case 0x34:
		case 0x35:
		case 0x36:
		case 0xB0:
			if(i+1 < cmdLen) {
				i+=dec[i+1]+2;
			}
			else {
				i++;
			}
			break;
		default:
			i++;
			continue;
		}
	}

	free(dec);

	if(l!=3) {
		return 1;
	}
	if(mecmAlgo>0) {
		return 1;
	}

	identMask = ident & 0xFF00;
	if (identMask == 0x1100 || identMask == 0x500 || identMask == 0x3100) {
		memcpy(&tmp1, dw, 4);
		memcpy(&tmp2, dw + 4, 4);
		memcpy(&tmp3, dw + 12, 4);
		memcpy(dw, dw + 8, 4);
		memcpy(dw + 4, &tmp3, 4);
		memcpy(dw + 8, &tmp1, 4);
		memcpy(dw + 12, &tmp2, 4);
	}
	return 0;
}

// Irdeto EMU
static int8_t GetIrdetoKey(uint8_t *buf, uint32_t ident, char keyName, uint32_t keyIndex, uint8_t isCriticalKey, uint8_t *keyRef)
{
	char keyStr[EMU_MAX_CHAR_KEYNAME];
	snprintf(keyStr, EMU_MAX_CHAR_KEYNAME, "%c%X", keyName, keyIndex);
	if(FindKey('I', ident, keyStr, buf, 16, *keyRef > 0 ? 0 : isCriticalKey, *keyRef, 0, NULL)) {
		(*keyRef)++;
		return 1;
	}

	return 0;
}

static void Irdeto2Encrypt(uint8_t *data, const uint8_t *seed, const uint8_t *okey, int32_t len)
{
	const uint8_t *tmp = seed;
	uint8_t key[16];
	int32_t i;

	memcpy(key, okey, 16);
	doPC1(key);
	doPC1(&key[8]);
	len&=~7;

	for(i=0; i+7<len; i+=8) {
		xxor(&data[i],8,&data[i],tmp);
		tmp=&data[i];
		des(key,DES_ECS2_CRYPT,&data[i]);
		des(&key[8],DES_ECS2_DECRYPT,&data[i]);
		des(key,DES_ECS2_CRYPT,&data[i]);
	}
}

static void Irdeto2Decrypt(uint8_t *data, const uint8_t *seed, const uint8_t *okey, int32_t len)
{
	uint8_t buf[2][8];
	uint8_t key[16];
	int32_t i, n=0;

	memcpy(key, okey, 16);
	doPC1(key);
	doPC1(&key[8]);
	len&=~7;

	memcpy(buf[n],seed,8);
	for(i=0; i+7<len; i+=8,data+=8,n^=1) {
		memcpy(buf[1-n],data,8);
		des(key,DES_ECS2_DECRYPT,data);
		des(&key[8],DES_ECS2_CRYPT,data);
		des(key,DES_ECS2_DECRYPT,data);
		xxor(data,8,data,buf[n]);
	}
}

static int8_t Irdeto2CalculateHash(const uint8_t *okey, const uint8_t *iv, const uint8_t *data, int32_t len)
{
	uint8_t cbuff[8];
	uint8_t key[16];
	int32_t l, y;

	memcpy(key, okey, 16);
	doPC1(key);
	doPC1(&key[8]);

	memset(cbuff,0,sizeof(cbuff));
	len-=8;

	for(y=0; y<len; y+=8) {
		if(y<len-8) {
			xxor(cbuff,8,cbuff,&data[y]);
		}
		else {
			l=len-y;
			xxor(cbuff,l,cbuff,&data[y]);
			xxor(cbuff+l,8-l,cbuff+l,iv+8);
		}
		des(key,DES_ECS2_CRYPT,cbuff);
		des(&key[8],DES_ECS2_DECRYPT,cbuff);
		des(key,DES_ECS2_CRYPT,cbuff);
	}

	return memcmp(cbuff,&data[len],8)==0;
}

static int8_t Irdeto2ECM(uint16_t caid, uint8_t *oecm, uint8_t *dw)
{
	uint8_t keyNr=0, length, end, key[16], okeySeed[16], keySeed[16], keyIV[16], tmp[16];
	uint32_t i, l, ident;
	uint8_t key0Ref, keySeedRef, keyIVRef;
	uint8_t ecmCopy[EMU_MAX_ECM_LEN], *ecm = oecm;
	uint16_t ecmLen = GetEcmLen(ecm);

	if(ecmLen < 12) {
		return 1;
	}

	length = ecm[11];
	keyNr = ecm[9];
	ident = ecm[8] | caid << 8;

	if(ecmLen < length+12) {
		return 1;
	}

	key0Ref = 0;
	while(GetIrdetoKey(key, ident, '0', keyNr, 1, &key0Ref)) {
		keySeedRef = 0;
		while(GetIrdetoKey(okeySeed, ident, 'M', 1, 1, &keySeedRef)) {
			keyIVRef = 0;
			while(GetIrdetoKey(keyIV, ident, 'M', 2, 1, &keyIVRef)) {

				memcpy(keySeed, okeySeed, 16);
				memcpy(ecmCopy, oecm, ecmLen);
				ecm = ecmCopy;

				memset(tmp, 0, 16);
				Irdeto2Encrypt(keySeed, tmp, key, 16);
				ecm+=12;
				Irdeto2Decrypt(ecm, keyIV, keySeed, length);
				i=(ecm[0]&7)+1;
				end = length-8 < 0 ? 0 : length-8;

				while(i<end) {
					l = ecm[i+1] ? (ecm[i+1]&0x3F)+2 : 1;
					switch(ecm[i]) {
					case 0x10:
					case 0x50:
						if(l==0x13 && i<=length-8-l) {
							Irdeto2Decrypt(&ecm[i+3], keyIV, key, 16);
						}
						break;
					case 0x78:
						if(l==0x14 && i<=length-8-l) {
							Irdeto2Decrypt(&ecm[i+4], keyIV, key, 16);
						}
						break;
					}
					i+=l;
				}

				i=(ecm[0]&7)+1;
				if(Irdeto2CalculateHash(keySeed, keyIV, ecm-6, length+6)) {
					while(i<end) {
						l = ecm[i+1] ? (ecm[i+1]&0x3F)+2 : 1;
						switch(ecm[i]) {
						case 0x78:
							if(l==0x14 && i<=length-8-l) {
								memcpy(dw, &ecm[i+4], 16);
								return 0;
							}
						}
						i+=l;
					}
				}
			}
			if(keyIVRef == 0) {
				return 2;
			}
		}
		if(keySeedRef == 0) {
			return 2;
		}
	}
	if(key0Ref == 0) {
		return 2;
	}

	return 1;
}

// BISS Emu
static int8_t BissECM(uint16_t UNUSED(caid), const uint8_t *ecm, int16_t ecmDataLen,
					  uint8_t *dw, uint16_t srvid, uint16_t ecmpid)
{
	uint8_t haveKey1 = 0, haveKey2 = 0;
	uint16_t ecmLen = 0, pid = 0;
	uint32_t i;

	//try using ecmpid if it seems to be valid
	if(ecmpid != 0) {
		haveKey1 = FindKey('F', (srvid<<16)|ecmpid, "00", dw, 8, 1, 0, 0, NULL);
		haveKey2 = FindKey('F', (srvid<<16)|ecmpid, "01", &dw[8], 8, 1, 0, 0, NULL);

		if(haveKey1 && haveKey2) {return 0;}
		else if(haveKey1 && !haveKey2) {memcpy(&dw[8], dw, 8); return 0;}
		else if(!haveKey1 && haveKey2) {memcpy(dw, &dw[8], 8); return 0;}
	}

	//try to get the pid from oscam's fake ecm ([sid] ([pid1] [pid2] ... [pidx])
	if(ecmDataLen >= 3) {
		ecmLen = GetEcmLen(ecm);

		if(ecmLen > 7 && ecmLen <= ecmDataLen) {
			for(i=5; i+1<ecmLen; i+=2) {
				pid = b2i(2, ecm+i);
				haveKey1 = FindKey('F', (srvid<<16)|pid, "00", dw, 8, 1, 0, 0, NULL);
				haveKey2 = FindKey('F', (srvid<<16)|pid, "01", &dw[8], 8, 1, 0, 0, NULL);

				if(haveKey1 && haveKey2) {return 0;}
				else if(haveKey1 && !haveKey2) {memcpy(&dw[8], dw, 8); return 0;}
				else if(!haveKey1 && haveKey2) {memcpy(dw, &dw[8], 8); return 0;}
			}
		}
	}

	//fallback to default pid
	haveKey1 = FindKey('F', (srvid<<16)|0x1FFF, "00", dw, 8, 1, 0, 0, NULL);
	haveKey2 = FindKey('F', (srvid<<16)|0x1FFF, "01", &dw[8], 8, 1, 0, 0, NULL);

	if(haveKey1 && haveKey2) {return 0;}
	else if(haveKey1 && !haveKey2) {memcpy(&dw[8], dw, 8); return 0;}
	else if(!haveKey1 && haveKey2) {memcpy(dw, &dw[8], 8); return 0;}

	return 2;
}

//PowerVu Emu
static int8_t GetPowervuKey(uint8_t *buf, uint32_t ident, char keyName, uint32_t keyIndex, uint32_t keyLength, uint8_t isCriticalKey)
{
	char keyStr[EMU_MAX_CHAR_KEYNAME];
	snprintf(keyStr, EMU_MAX_CHAR_KEYNAME, "%c%X", keyName, keyIndex);
	if(FindKey('P', ident, keyStr, buf, keyLength, isCriticalKey, 0, 0, NULL)) {
		return 1;
	}

	return 0;
}

static int8_t GetPowervuEmmKey(uint8_t *buf, uint32_t ident, char *keyName, uint32_t keyLength, uint8_t isCriticalKey, uint32_t *getProvider)
{
	if(FindKey('P', ident, keyName, buf, keyLength, isCriticalKey, 0, 0, getProvider)) {
		return 1;
	}

	return 0;
}

static const uint8_t PowerVu_A0_S_1[16] = {0x33, 0xA4, 0x44, 0x3C, 0xCA, 0x2E, 0x75, 0x7B, 0xBC, 0xE6, 0xE5, 0x35, 0xA0, 0x55, 0xC9, 0xA2};
static const uint8_t PowerVu_A0_S_2[16] = {0x5A, 0xB0, 0x2C, 0xBC, 0xDA, 0x32, 0xE6, 0x92, 0x40, 0x53, 0x6E, 0xF9, 0x69, 0x11, 0x1E, 0xFB};
static const uint8_t PowerVu_A0_S_3[16] = {0x4E, 0x18, 0x9B, 0x19, 0x79, 0xFB, 0x01, 0xFA, 0xE3, 0xE1, 0x28, 0x3D, 0x32, 0xE4, 0x92, 0xEA};
static const uint8_t PowerVu_A0_S_4[16] = {0x05, 0x6F, 0x37, 0x66, 0x35, 0xE1, 0x58, 0xD0, 0xB4, 0x6A, 0x97, 0xAE, 0xD8, 0x91, 0x27, 0x56};
static const uint8_t PowerVu_A0_S_5[16] = {0x7B, 0x26, 0xAD, 0x34, 0x3D, 0x77, 0x39, 0x51, 0xE0, 0xE0, 0x48, 0x8C, 0x39, 0xF5, 0xE8, 0x47};
static const uint8_t PowerVu_A0_S_6[16] = {0x74, 0xFA, 0x4D, 0x79, 0x42, 0x39, 0xD1, 0xA4, 0x99, 0xA3, 0x97, 0x07, 0xDF, 0x14, 0x3A, 0xC4};
static const uint8_t PowerVu_A0_S_7[16] = {0xC6, 0x1E, 0x3C, 0x24, 0x11, 0x08, 0x5D, 0x6A, 0xEB, 0x97, 0xB9, 0x25, 0xA7, 0xFA, 0xE9, 0x1A};
static const uint8_t PowerVu_A0_S_8[16] = {0x9A, 0xAD, 0x72, 0xD7, 0x7C, 0x68, 0x3B, 0x55, 0x1D, 0x4A, 0xA2, 0xB0, 0x38, 0xB9, 0x56, 0xD0};
static const uint8_t PowerVu_A0_S_9[32] = {0x61, 0xDA, 0x5F, 0xB7, 0xEB, 0xC6, 0x3F, 0x6C, 0x09, 0xF3, 0x64, 0x38, 0x33, 0x08, 0xAA, 0x15,
										   0xCC, 0xEF, 0x22, 0x64, 0x01, 0x2C, 0x12, 0xDE, 0xF4, 0x6E, 0x3C, 0xCD, 0x1A, 0x64, 0x63, 0x7C
										  };

static const uint8_t PowerVu_00_S_1[16] = {0x97, 0x13, 0xEB, 0x6B, 0x04, 0x5E, 0x60, 0x3A, 0xD9, 0xCC, 0x91, 0xC2, 0x5A, 0xFD, 0xBA, 0x0C};
static const uint8_t PowerVu_00_S_2[16] = {0x61, 0x3C, 0x03, 0xB0, 0xB5, 0x6F, 0xF8, 0x01, 0xED, 0xE0, 0xE5, 0xF3, 0x78, 0x0F, 0x0A, 0x73};
static const uint8_t PowerVu_00_S_3[16] = {0xFD, 0xDF, 0xD2, 0x97, 0x06, 0x14, 0x91, 0xB5, 0x36, 0xAD, 0xBC, 0xE1, 0xB3, 0x00, 0x66, 0x41};
static const uint8_t PowerVu_00_S_4[16] = {0x8B, 0xD9, 0x18, 0x0A, 0xED, 0xEE, 0x61, 0x34, 0x1A, 0x79, 0x80, 0x8C, 0x1E, 0x7F, 0xC5, 0x9F};
static const uint8_t PowerVu_00_S_5[16] = {0xB0, 0xA1, 0xF2, 0xB8, 0xEA, 0x72, 0xDD, 0xD3, 0x30, 0x65, 0x2B, 0x1E, 0xE9, 0xE1, 0x45, 0x29};
static const uint8_t PowerVu_00_S_6[16] = {0x5D, 0xCA, 0x53, 0x75, 0xB2, 0x24, 0xCE, 0xAF, 0x21, 0x54, 0x9E, 0xBE, 0x02, 0xA9, 0x4C, 0x5D};
static const uint8_t PowerVu_00_S_7[16] = {0x42, 0x66, 0x72, 0x83, 0x1B, 0x2D, 0x22, 0xC9, 0xF8, 0x4D, 0xBA, 0xCD, 0xBB, 0x20, 0xBD, 0x6B};
static const uint8_t PowerVu_00_S_8[16] = {0xC4, 0x0C, 0x6B, 0xD3, 0x6D, 0x94, 0x7E, 0x53, 0xCE, 0x96, 0xAC, 0x40, 0x2C, 0x7A, 0xD3, 0xA9};
static const uint8_t PowerVu_00_S_9[32] = {0x31, 0x82, 0x4F, 0x9B, 0xCB, 0x6F, 0x9D, 0xB7, 0xAE, 0x68, 0x0B, 0xA0, 0x93, 0x15, 0x32, 0xE2,
										   0xED, 0xE9, 0x47, 0x29, 0xC2, 0xA8, 0x92, 0xEF, 0xBA, 0x27, 0x22, 0x57, 0x76, 0x54, 0xC0, 0x59,
										  };

static uint8_t PowervuSbox(uint8_t *input, uint8_t mode)
{
	uint8_t s_index, bit, last_index, last_bit;
	uint8_t const *Sbox1, *Sbox2, *Sbox3, *Sbox4, *Sbox5, *Sbox6, *Sbox7, *Sbox8, *Sbox9;

	if(mode == 0xA0)
	{
		Sbox1 = PowerVu_A0_S_1;
		Sbox2 = PowerVu_A0_S_2;
		Sbox3 = PowerVu_A0_S_3;
		Sbox4 = PowerVu_A0_S_4;
		Sbox5 = PowerVu_A0_S_5;
		Sbox6 = PowerVu_A0_S_6;
		Sbox7 = PowerVu_A0_S_7;
		Sbox8 = PowerVu_A0_S_8;
		Sbox9 = PowerVu_A0_S_9;
	}
	else
	{
		Sbox1 = PowerVu_00_S_1;
		Sbox2 = PowerVu_00_S_2;
		Sbox3 = PowerVu_00_S_3;
		Sbox4 = PowerVu_00_S_4;
		Sbox5 = PowerVu_00_S_5;
		Sbox6 = PowerVu_00_S_6;
		Sbox7 = PowerVu_00_S_7;
		Sbox8 = PowerVu_00_S_8;
		Sbox9 = PowerVu_00_S_9;
	}

	bit = (GetBit(input[2],0)<<2) | (GetBit(input[3],4)<<1) | (GetBit(input[5],3));
	s_index = (GetBit(input[0],0)<<3) | (GetBit(input[2],6)<<2) | (GetBit(input[2],4)<<1) | (GetBit(input[5],7));
	last_bit = GetBit(Sbox1[s_index],7-bit);

	bit = (GetBit(input[5],0)<<2) | (GetBit(input[4],0)<<1) | (GetBit(input[6],2));
	s_index = (GetBit(input[2],1)<<3) | (GetBit(input[2],2)<<2) | (GetBit(input[5],5)<<1) | (GetBit(input[5],1));
	last_bit = last_bit | (GetBit(Sbox2[s_index],7-bit)<<1);

	bit = (GetBit(input[6],0)<<2) | (GetBit(input[1],7)<<1) | (GetBit(input[6],7));
	s_index = (GetBit(input[1],3)<<3) | (GetBit(input[3],7)<<2) | (GetBit(input[1],5)<<1) | (GetBit(input[5],2));
	last_bit = last_bit | (GetBit(Sbox3[s_index], 7-bit)<<2);

	bit = (GetBit(input[1],0)<<2) | (GetBit(input[2],7)<<1) | (GetBit(input[2],5));
	s_index = (GetBit(input[6],3)<<3) | (GetBit(input[6],4)<<2) | (GetBit(input[6],6)<<1) | (GetBit(input[3],5));
	last_index = GetBit(Sbox4[s_index], 7-bit);

	bit = (GetBit(input[3],3)<<2) | (GetBit(input[4],6)<<1) | (GetBit(input[3],2));
	s_index = (GetBit(input[3],1)<<3) | (GetBit(input[4],5)<<2) | (GetBit(input[3],0)<<1) | (GetBit(input[4],7));
	last_index = last_index | (GetBit(Sbox5[s_index], 7-bit)<<1);

	bit = (GetBit(input[5],4)<<2) | (GetBit(input[4],4)<<1) | (GetBit(input[1],2));
	s_index = (GetBit(input[2],3)<<3) | (GetBit(input[6],5)<<2) | (GetBit(input[1],4)<<1) | (GetBit(input[4],1));
	last_index = last_index | (GetBit(Sbox6[s_index], 7-bit)<<2);

	bit = (GetBit(input[0],6)<<2) | (GetBit(input[0],7)<<1) | (GetBit(input[0],4));
	s_index = (GetBit(input[0],5)<<3) | (GetBit(input[0],3)<<2) | (GetBit(input[0],1)<<1) | (GetBit(input[0],2));
	last_index = last_index | (GetBit(Sbox7[s_index], 7-bit)<<3);

	bit = (GetBit(input[4],2)<<2) | (GetBit(input[4],3)<<1) | (GetBit(input[1],1));
	s_index = (GetBit(input[1],6)<<3) | (GetBit(input[6],1)<<2) | (GetBit(input[5],6)<<1) | (GetBit(input[3],6));
	last_index = last_index | (GetBit(Sbox8[s_index], 7-bit)<<4);

	return (GetBit(Sbox9[last_index&0x1f],7-last_bit)&1) ? 1: 0;
}

static void PowervuDecrypt(uint8_t *data, uint32_t length, uint8_t *key)
{
	uint32_t i;
	int32_t j, k;
	uint8_t curByte, tmpBit;

	for(i=0; i<length; i++)
	{
		curByte = data[i];

		for(j=7; j>=0; j--)
		{
			data[i] = SetBit(data[i], j,(GetBit(curByte,j)^PowervuSbox(key, 0))^GetBit(key[0],7));

			tmpBit = GetBit(data[i],j)^(GetBit(key[6],0));
			if (tmpBit)
			{
				key[3] ^= 0x10;
			}

			for (k = 6; k > 0; k--)
			{
				key[k] = (key[k]>>1) | (key[k-1]<<7);
			}
			key[0] = (key[0]>>1);

			key[0] = SetBit(key[0], 7, tmpBit);
		}
	}
}

#define PVU_CW_VID 0	// VIDeo
#define PVU_CW_HSD 1	// High Speed Data
#define PVU_CW_A1 2		// Audio 1
#define PVU_CW_A2 3		// Audio 2
#define PVU_CW_A3 4		// Audio 3
#define PVU_CW_A4 5		// Audio 4
#define PVU_CW_UTL 6	// UTiLity
#define PVU_CW_VBI 7	// Vertical Blanking Interval

#define PVU_CONVCW_VID_ECM 0x80	// VIDeo
#define PVU_CONVCW_HSD_ECM 0x40 // High Speed Data
#define PVU_CONVCW_A1_ECM 0x20	// Audio 1
#define PVU_CONVCW_A2_ECM 0x10	// Audio 2
#define PVU_CONVCW_A3_ECM 0x08	// Audio 3
#define PVU_CONVCW_A4_ECM 0x04	// Audio 4
#define PVU_CONVCW_UTL_ECM 0x02	// UTiLity
#define PVU_CONVCW_VBI_ECM 0x01	// Vertical Blanking Interval

static uint8_t PowervuGetConvcwIndex(uint8_t ecmTag)
{
	switch(ecmTag)
	{
	case PVU_CONVCW_VID_ECM:
		return PVU_CW_VID;

	case PVU_CONVCW_HSD_ECM:
		return PVU_CW_HSD;

	case PVU_CONVCW_A1_ECM:
		return PVU_CW_A1;

	case PVU_CONVCW_A2_ECM:
		return PVU_CW_A2;

	case PVU_CONVCW_A3_ECM:
		return PVU_CW_A3;

	case PVU_CONVCW_A4_ECM:
		return PVU_CW_A4;

	case PVU_CONVCW_UTL_ECM:
		return PVU_CW_UTL;

	case PVU_CONVCW_VBI_ECM:
		return PVU_CW_VBI;

	default:
		return PVU_CW_VBI;
	}
}

static uint16_t PowervuGetSeedIV(uint8_t seedType, uint8_t *ecm)
{
	switch(seedType)
	{
	case PVU_CW_VID:
		return ((ecm[0x10] & 0x1F) <<3) | 0;
	case PVU_CW_HSD:
		return ((ecm[0x12] & 0x1F) <<3) | 2;
	case PVU_CW_A1:
		return ((ecm[0x11] & 0x3F) <<3) | 1;
	case PVU_CW_A2:
		return ((ecm[0x13] & 0x3F) <<3) | 1;
	case PVU_CW_A3:
		return ((ecm[0x19] & 0x3F) <<3) | 1;
	case PVU_CW_A4:
		return ((ecm[0x1A] & 0x3F) <<3) | 1;;
	case PVU_CW_UTL:
		return ((ecm[0x14] & 0x0F) <<3) | 4;
	case PVU_CW_VBI:
		return (((ecm[0x15] & 0xF8)>>3)<<3) | 5;
	default:
		return 0;
	}
}

static void PowervuExpandSeed(uint8_t seedType, uint8_t *seed)
{
	uint8_t seedLength, i;

	switch(seedType)
	{
	case PVU_CW_VID:
	case PVU_CW_HSD:
		seedLength = 4;
		break;
	case PVU_CW_A1:
	case PVU_CW_A2:
	case PVU_CW_A3:
	case PVU_CW_A4:
		seedLength = 3;
		break;
	case PVU_CW_UTL:
	case PVU_CW_VBI:
		seedLength = 2;
		break;
	default:
		return;
	}

	for(i=seedLength; i<7; i++)
	{
		seed[i] = seed[i%seedLength];
	}
}

static void PowervuCalculateSeed(uint8_t seedType, uint8_t *ecm, uint8_t *seedBase, uint8_t *key, uint8_t *seed)
{
	uint16_t tmpSeed;

	tmpSeed = PowervuGetSeedIV(seedType, ecm+23);
	seed[0] = (tmpSeed>>2) & 0xFF;
	seed[1] = ((tmpSeed&0x3)<<6) | (seedBase[0]>>2);
	seed[2] = (seedBase[0]<<6) | (seedBase[1]>>2);
	seed[3] = (seedBase[1]<<6) | (seedBase[2]>>2);
	seed[4] = (seedBase[2]<<6) | (seedBase[3]>>2);
	seed[5] = (seedBase[3]<<6);

	PowervuDecrypt(seed, 6, key);

	seed[0] = (seed[1]<<2) | (seed[2]>>6);
	seed[1] = (seed[2]<<2) | (seed[3]>>6);
	seed[2] = (seed[3]<<2) | (seed[4]>>6);
	seed[3] = (seed[4]<<2) | (seed[5]>>6);
}

static void PowervuCalculateCw(uint8_t seedType, uint8_t *seed, uint8_t csaUsed, uint8_t convolvedCwAvailable,
							   uint8_t *convolvedCw, uint8_t *cw, uint8_t *baseCw)
{
	int32_t k;

	PowervuExpandSeed(seedType, seed);

	if(csaUsed)
	{
		if(!convolvedCwAvailable)
		{
			return;
		}
		cw[0] = seed[0] ^ convolvedCw[0];
		cw[1] = seed[1] ^ convolvedCw[1];
		cw[2] = seed[2] ^ convolvedCw[2];
		cw[3] = seed[3] ^ convolvedCw[3];
		cw[4] = seed[3] ^ convolvedCw[4];
		cw[5] = seed[4] ^ convolvedCw[5];
		cw[6] = seed[5] ^ convolvedCw[6];
		cw[7] = seed[6] ^ convolvedCw[7];
	}
	else
	{
		for(k=0; k<7; k++)
		{
			cw[k] = seed[k] ^ baseCw[k];
		}
		ExpandDesKey(cw);
	}
}

static int8_t PowervuECM(uint8_t *ecm, uint8_t *dw)
{
	uint16_t ecmLen = GetEcmLen(ecm);
	uint32_t ecmCrc32;
	uint8_t nanoCmd, nanoChecksum, keyType, fixedKey, oddKey, bid, keyIndex, csaUsed;
	uint16_t nanoLen, channelId;
	uint32_t i, j;
	uint8_t convolvedCw[8][8];
	uint8_t convolvedCwAvailable[8];
	uint8_t ecmKey[7], tmpEcmKey[7], seedBase[4], baseCw[7], seed[8][8], cw[8][8];

	if(ecmLen < 7)
	{
		return 1;
	}

	ecmCrc32 = b2i(4, ecm+ecmLen-4);

	if(fletcher_crc32(ecm, ecmLen-4) != ecmCrc32)
	{
		return 8;
	}
	ecmLen -= 4;

	memset(convolvedCwAvailable, 0, 8);

	for(i=3; i+3<ecmLen; ) {
		nanoLen = (((ecm[i] & 0x0f)<< 8) | ecm[i+1]);
		i +=2;
		if(nanoLen > 0)
		{
			nanoLen--;
		}
		nanoCmd = ecm[i++];
		if(i+nanoLen > ecmLen) {
			return 1;
		}

		switch (nanoCmd) {
		case 0x27:
			if(nanoLen < 15)
			{
				break;
			}

			nanoChecksum = 0;
			for(j=4; j<15; j++)
			{
				nanoChecksum += ecm[i+j];
			}

			if(nanoChecksum != 0)
			{
				break;
			}

			keyType = PowervuGetConvcwIndex(ecm[i+4]);
			memcpy(convolvedCw[keyType], &ecm[i+6], 8);
			convolvedCwAvailable[keyType] = 1;
			break;

		default:
			break;
		}
		i += nanoLen;
	}

	for(i=3; i+3<ecmLen; ) {
		nanoLen = (((ecm[i] & 0x0f)<< 8) | ecm[i+1]);
		i +=2;
		if(nanoLen > 0)
		{
			nanoLen--;
		}
		nanoCmd = ecm[i++];
		if(i+nanoLen > ecmLen) {
			return 1;
		}

		switch (nanoCmd) {
		case 0x20:
			if(nanoLen < 54)
			{
				break;
			}

			csaUsed = GetBit(ecm[i+7], 7);
			if(csaUsed && !convolvedCwAvailable[PVU_CW_VID] && !convolvedCwAvailable[PVU_CW_A1])
			{
				break;
			}

			fixedKey = !GetBit(ecm[i+6], 5);
			oddKey = GetBit(ecm[i+6], 4);
			bid = (GetBit(ecm[i+7], 1)<<1) | GetBit(ecm[i+7], 0);

			keyIndex = (fixedKey<<3) | (bid<<2) | oddKey;
			channelId = b2i(2, ecm+i+23);
			if(!GetPowervuKey(ecmKey, channelId, '0', keyIndex, 7, 1))
			{
				break;
			}

			PowervuDecrypt(ecm+i+8, 14, ecmKey);
			if((ecm[i+6] != ecm[i+6+7]) || (ecm[i+6+8] != ecm[i+6+15]))
			{
				break;
			}
			memcpy(tmpEcmKey, ecmKey, 7);

			PowervuDecrypt(ecm+i+27, 27, ecmKey);
			if((ecm[i+23] != ecm[i+23+29]) || (ecm[i+23+1] != ecm[i+23+30]))
			{
				break;
			}

			memcpy(seedBase, ecm+i+6+2, 4);

			// Calculate all seeds
			//for(j=0; j<8; j++)
			//{
			//	memcpy(ecmKey, tmpEcmKey, 7);
			//	PowervuCalculateSeed(j, ecm+i, seedBase, ecmKey, seed[j]);
			//}

			// Calculate only video and audio1 seed
			memcpy(ecmKey, tmpEcmKey, 7);
			PowervuCalculateSeed(PVU_CW_VID, ecm+i, seedBase, ecmKey, seed[PVU_CW_VID]);
			memcpy(ecmKey, tmpEcmKey, 7);
			PowervuCalculateSeed(PVU_CW_A1, ecm+i, seedBase, ecmKey, seed[PVU_CW_A1]);

			memcpy(baseCw, ecm+i+6+8, 7);

			// Calculate all cws
			//for(j=0; j<8; j++)
			//{
			//	PowervuCalculateCw(j,  seed[j], csaUsed, convolvedCwAvailable[j], convolvedCw[j], cw[j], baseCw);
			//}

			// Calculate only video and audio1 cw
			PowervuCalculateCw(PVU_CW_VID, seed[PVU_CW_VID], csaUsed, convolvedCwAvailable[PVU_CW_VID], convolvedCw[PVU_CW_VID], cw[PVU_CW_VID], baseCw);
			PowervuCalculateCw(PVU_CW_A1, seed[PVU_CW_A1], csaUsed, convolvedCwAvailable[PVU_CW_A1], convolvedCw[PVU_CW_A1], cw[PVU_CW_A1], baseCw);

			if(csaUsed)
			{
				if(convolvedCwAvailable[PVU_CW_VID])
				{
					memcpy(dw, cw[PVU_CW_VID], 8);
				}

				if(convolvedCwAvailable[PVU_CW_A1])
				{
					memcpy(&dw[8], cw[PVU_CW_A1], 8);
				}

				for(i = 0; i < 16; i += 4) {
					dw[i + 3] = ((dw[i] + dw[i + 1] + dw[i + 2]) & 0xff);
				}
			}
			else
			{
				memcpy(dw, cw[PVU_CW_VID], 8);
				memcpy(&dw[8], cw[PVU_CW_A1], 8);
			}

			//cs_log_dump(cw[PVU_CW_VID], 8, "Final VideoCW: ");
			//cs_log_dump(cw[PVU_CW_A1], 8, "Final Audio1CW: ");

			return 0;

		default:
			break;
		}
		i += nanoLen;
	}

	return 1;
}

char* GetProcessECMErrorReason(int8_t result)
{
	switch(result) {
	case 0:
		return "No error";
	case 1:
		return "ECM not supported";
	case 2:
		return "Key not found";
	case 3:
		return "Nano80 problem";
	case 4:
		return "Corrupt data";
	case 5:
		return "CW not found";
	case 6:
		return "CW checksum error";
	case 7:
		return "Out of memory";
	case 8:
		return "ECM checksum error";
	default:
		return "Unknown";
	}
}

/* Error codes
0  OK
1  ECM not supported
2  Key not found
3  Nano80 problem
4  Corrupt data
5  CW not found
6  CW checksum error
7  Out of memory
*/
int8_t ProcessECM(int16_t ecmDataLen, uint16_t caid, uint32_t UNUSED(provider), const uint8_t *ecm,
				  uint8_t *dw, uint16_t srvid, uint16_t ecmpid)
{
	int8_t result = 1, i;
	uint8_t ecmCopy[EMU_MAX_ECM_LEN];
	uint16_t ecmLen = 0;

	if(ecmDataLen < 3) {
		// accept requests without ecm only for biss
		if((caid>>8) != 0x26 && caid != 0xFFFF) {
			return 1;
		}
	}
	else {
		ecmLen = GetEcmLen(ecm);
	}

	if(ecmLen > ecmDataLen) {
		return 1;
	}

	if(ecmLen > EMU_MAX_ECM_LEN) {
		return 1;
	}
	memcpy(ecmCopy, ecm, ecmLen);

	if((caid>>8)==0x0D) {
		result = CryptoworksECM(caid,ecmCopy,dw);
	}
	else if((caid>>8)==0x09) {
		result = SoftNDSECM(caid,ecmCopy,dw);
	}
	else if(caid==0x0500) {
		result = ViaccessECM(ecmCopy,dw);
	}
	else if((caid>>8)==0x18) {
		result = Nagra2ECM(ecmCopy,dw);
	}
	else if((caid>>8)==0x06) {
		result = Irdeto2ECM(caid,ecmCopy,dw);
	}
	else if((caid>>8)==0x26 || caid == 0xFFFF) {
		result = BissECM(caid,ecm,ecmDataLen,dw,srvid,ecmpid);
	}
	else if((caid>>8)==0x0E) {
		result = PowervuECM(ecmCopy,dw);
	}

	// fix dcw checksum
	if(result == 0 && !(caid>>8)==0x0E) {
		for(i = 0; i < 16; i += 4) {
			dw[i + 3] = ((dw[i] + dw[i + 1] + dw[i + 2]) & 0xff);
		}
	}

	if(result != 0) {
		cs_log("[Emu] ECM failed: %s", GetProcessECMErrorReason(result));
	}

	return result;
}

// Viaccess EMM EMU
static int8_t ViaccessEMM(uint8_t *emm, uint32_t *keysAdded)
{
	uint8_t nanoCmd = 0, subNanoCmd = 0, *tmp, *newKeyD0, *newEcmKey;
	uint16_t i = 0, j = 0, k = 0, emmLen = GetEcmLen(emm);
	uint8_t ecmKeys[6][16], keyD0[2], emmKey[16], emmXorKey[16], provName[17];
	uint8_t ecmKeyCount = 0, emmKeyIndex = 0, aesMode = 0x0D;
	uint8_t nanoLen = 0, subNanoLen = 0, haveEmmXorKey = 0, haveNewD0 = 0;
	uint32_t ui1, ui2, ui3, ecmKeyIndex[6], provider = 0, ecmProvider = 0;
	char keyName[EMU_MAX_CHAR_KEYNAME], keyValue[36];
	struct aes_keys aes;

	memset(keyD0, 0, 2);
	memset(ecmKeyIndex, 0, sizeof(uint32_t)*6);

	for(i=3; i+2<emmLen; ) {
		nanoCmd = emm[i++];
		nanoLen = emm[i++];
		if(i+nanoLen > emmLen) {
			return 1;
		}

		switch(nanoCmd) {
		case 0x90: {
			if(nanoLen < 3) {
				break;
			}
			ui1 = emm[i+2];
			ui2 = emm[i+1];
			ui3 = emm[i];
			provider = (ui1 | (ui2 << 8) | (ui3 << 16));
			if(provider == 0x00D00040) {
				ecmProvider = 0x030B00;
			}
			else {
				return 1;
			}
			break;
		}
		case 0xD2: {
			if(nanoLen < 2) {
				break;
			}
			emmKeyIndex = emm[i+1];
			break;
		}
		case 0x41: {
			if(nanoLen < 1) {
				break;
			}
			if(!GetViaKey(emmKey, provider, 'M', emmKeyIndex, 16, 1)) {
				return 2;
			}
			memset(provName, 0, 17);
			memset(emmXorKey, 0, 16);
			k = nanoLen < 16 ? nanoLen : 16;
			memcpy(provName, &emm[i], k);
			aes_set_key(&aes, (char*)emmKey);
			aes_decrypt(&aes, emmXorKey, 16);
			for(j=0; j<16; j++) {
				provName[j] ^= emmXorKey[j];
			}
			provName[k] = 0;

			if(strcmp((char*)provName, "TNTSAT") != 0 && strcmp((char*)provName, "TNTSATPRO") != 0
					&&strcmp((char*)provName, "CSAT V") != 0) {
				return 1;
			}
			break;
		}
		case 0xBA: {
			if(nanoLen < 2) {
				break;
			}
			GetViaKey(keyD0, ecmProvider, 'D', 0, 2, 0);
			ui1 = (emm[i] << 8) | emm[i+1];
			if( (uint32_t)((keyD0[0] << 8) | keyD0[1]) < ui1 || (keyD0[0] == 0x00 && keyD0[1] == 0x00)) {
				keyD0[0] = emm[i];
				keyD0[1] = emm[i+1];
				haveNewD0 = 1;
				break;
			}
			return 0;
		}
		case 0xBC: {
			break;
		}
		case 0x43: {
			if(nanoLen < 16) {
				break;
			}
			memcpy(emmXorKey, &emm[i], 16);
			haveEmmXorKey = 1;
			break;
		}
		case 0x44: {
			if(nanoLen < 3) {
				break;
			}
			if (!haveEmmXorKey) {
				memset(emmXorKey, 0, 16);
			}
			tmp = (uint8_t*)malloc(((nanoLen/16)+1)*16*sizeof(uint8_t));
			if(tmp == NULL) {
				return 7;
			}
			memcpy(tmp, &emm[i], nanoLen);
			aes_set_key(&aes, (char*)emmKey);
			for(j=0; j<nanoLen; j+=16) {
				aes_decrypt(&aes, emmXorKey, 16);
				for(k=0; k<16; k++) {
					tmp[j+k] ^= emmXorKey[k];
				}
			}
			memcpy(&emm[i-2], tmp, nanoLen);
			free(tmp);
			nanoLen = 0;
			i -= 2;
			break;
		}
		case 0x68: {
			if(ecmKeyCount > 5) {
				break;
			}
			for(j=i; j+2<i+nanoLen; ) {
				subNanoCmd = emm[j++];
				subNanoLen = emm[j++];
				if(j+subNanoLen > i+nanoLen) {
					break;
				}
				switch(subNanoCmd) {
				case 0xD2: {
					if(nanoLen < 2) {
						break;
					}
					aesMode = emm[j];
					emmKeyIndex = emm[j+1];
					break;
				}
				case 0x01: {
					if(nanoLen < 17) {
						break;
					}
					ecmKeyIndex[ecmKeyCount] = emm[j];
					memcpy(&ecmKeys[ecmKeyCount], &emm[j+1], 16);
					if(!GetViaKey(emmKey, provider, 'M', emmKeyIndex, 16, 1)) {
						break;
					}

					if(aesMode == 0x0F || aesMode == 0x11) {
						hdSurEncPhase1_D2_0F_11(ecmKeys[ecmKeyCount]);
						hdSurEncPhase2_D2_0F_11(ecmKeys[ecmKeyCount]);
					}
					else if(aesMode == 0x13 || aesMode == 0x15) {
						hdSurEncPhase1_D2_13_15(ecmKeys[ecmKeyCount]);
					}
					aes_set_key(&aes, (char*)emmKey);
					aes_decrypt(&aes, ecmKeys[ecmKeyCount], 16);
					if(aesMode == 0x0F || aesMode == 0x11) {
						hdSurEncPhase1_D2_0F_11(ecmKeys[ecmKeyCount]);
					}
					else if(aesMode == 0x13 || aesMode == 0x15) {
						hdSurEncPhase2_D2_13_15(ecmKeys[ecmKeyCount]);
					}

					ecmKeyCount++;
					break;
				}
				default:
					break;
				}
				j += subNanoLen;
			}
			break;
		}
		case 0xF0: {
			if(nanoLen != 4) {
				break;
			}
			ui1 = ((emm[i+2] << 8) | (emm[i+1] << 16) | (emm[i] << 24) | emm[i+3]);
			if(fletcher_crc32(emm + 3, emmLen - 11) != ui1) {
				return 4;
			}

			if(haveNewD0) {
				newKeyD0 = (uint8_t*)malloc(sizeof(uint8_t)*2);
				if(newKeyD0 == NULL) {
					return 7;
				}
				memcpy(newKeyD0, keyD0, 2);
				if(!SetKey('V', ecmProvider, "D0", newKeyD0, 2, 1)) {
					free(newKeyD0);
				}
				for(j=0; j<ecmKeyCount; j++) {
					newEcmKey = (uint8_t*)malloc(sizeof(uint8_t)*16);
					if(newEcmKey == NULL) {
						return 7;
					}
					memcpy(newEcmKey, ecmKeys[j], 16);
					snprintf(keyName, EMU_MAX_CHAR_KEYNAME, "E%X", ecmKeyIndex[j]);
					if(!SetKey('V', ecmProvider, keyName, newEcmKey, 16, 1)) {
						free(newEcmKey);
					}
					(*keysAdded)++;
					cs_hexdump(0, ecmKeys[j], 16, keyValue, sizeof(keyValue));
					cs_log("[Emu] Key found in EMM: V %06X %s %s", ecmProvider, keyName, keyValue);
				}
			}
			break;
		}
		default:
			break;
		}
		i += nanoLen;
	}
	return 0;
}

// Irdeto2 EMM EMU
static int8_t Irdeto2DoEMMTypeOP(uint32_t ident, uint8_t *emm, uint8_t *keySeed, uint8_t *keyIV, uint8_t *keyPMK,
								 uint16_t emmLen, uint8_t startOffset, uint8_t length, uint32_t *keysAdded)
{
	uint32_t end, i, l;
	uint8_t tmp[16], *newOpKey;
	char keyName[EMU_MAX_CHAR_KEYNAME], keyValue[36];

	memset(tmp, 0, 16);
	Irdeto2Encrypt(keySeed, tmp, keyPMK, 16);
	Irdeto2Decrypt(&emm[startOffset], keyIV, keySeed, length);

	i = 16;
	end = startOffset + (length-8 < 0 ? 0 : length-8);

	while(i<end) {
		l = emm[i+1] ? (emm[i+1]&0x3F)+2 : 1;
		switch(emm[i]) {
		case 0x10:
		case 0x50:
			if(l==0x13 && i<=startOffset+length-8-l) {
				Irdeto2Decrypt(&emm[i+3], keyIV, keyPMK, 16);
			}
			break;
		case 0x78:
			if(l==0x14 && i<=startOffset+length-8-l) {
				Irdeto2Decrypt(&emm[i+4], keyIV, keyPMK, 16);
			}
			break;
		}
		i+=l;
	}

	memmove(emm+6, emm+7, emmLen-7);

	i = 15;
	end = startOffset + (length-9 < 0 ? 0 : length-9);

	if(Irdeto2CalculateHash(keySeed, keyIV, emm+3, emmLen-4)) {
		while(i<end) {
			l = emm[i+1] ? (emm[i+1]&0x3F)+2 : 1;
			switch(emm[i]) {
			case 0x10:
			case 0x50:
				if(l==0x13 && i<=startOffset+length-9-l) {
					newOpKey = (uint8_t*)malloc(sizeof(uint8_t)*16);
					if(newOpKey == NULL) {
						return 7;
					}
					memcpy(newOpKey, &emm[i+3], 16);
					snprintf(keyName, EMU_MAX_CHAR_KEYNAME, "%02X", emm[i+2]>>2);
					if(!SetKey('I', ident, keyName, newOpKey, 16, 1)) {
						free(newOpKey);
					}
					(*keysAdded)++;
					cs_hexdump(0, &emm[i+3], 16, keyValue, sizeof(keyValue));
					cs_log("[Emu] Key found in EMM: I %06X %s %s", ident, keyName, keyValue);
				}
			}
			i+=l;
		}

		if(*keysAdded > 0) {
			return 0;
		}
	}

	return 1;
}

static int8_t Irdeto2DoEMMTypePMK(uint32_t ident, uint8_t *emm, uint8_t *keySeed, uint8_t *keyIV, uint8_t *keyPMK,
								  uint16_t emmLen, uint8_t startOffset, uint8_t length, uint32_t *keysAdded)
{
	uint32_t end, i, l, j;
	uint8_t *newPmkKey;
	char keyName[EMU_MAX_CHAR_KEYNAME], keyValue[36];

	Irdeto2Decrypt(&emm[startOffset], keyIV, keySeed, length);

	i = 13;
	end = startOffset + (length-8 < 0 ? 0 : length-8);

	while(i<end) {
		l = emm[i+1] ? (emm[i+1]&0x3F)+2 : 1;
		switch(emm[i]) {
		case 0x10:
		case 0x50:
			if(l==0x13 && i<=startOffset+length-8-l) {
				Irdeto2Decrypt(&emm[i+3], keyIV, keyPMK, 16);
			}
			break;
		case 0x78:
			if(l==0x14 && i<=startOffset+length-8-l) {
				Irdeto2Decrypt(&emm[i+4], keyIV, keyPMK, 16);
			}
			break;
		case 0x68:
			if(l==0x26 && i<=startOffset+length-8-l) {
				Irdeto2Decrypt(&emm[i+3], keyIV, keyPMK, 16*2);
			}
			break;
		}
		i+=l;
	}

	memmove(emm+7, emm+9, emmLen-9);

	i = 11;
	end = startOffset + (length-10 < 0 ? 0 : length-10);

	if(Irdeto2CalculateHash(keySeed, keyIV, emm+3, emmLen-5)) {
		while(i<end) {
			l = emm[i+1] ? (emm[i+1]&0x3F)+2 : 1;
			switch(emm[i]) {
			case 0x68:
				if(l==0x26 && i<=startOffset+length-10-l) {
					for(j=0; j<2; j++) {
						newPmkKey = (uint8_t*)malloc(sizeof(uint8_t)*16);
						if(newPmkKey == NULL) {
							return 7;
						}
						memcpy(newPmkKey, &emm[i+3+j*16], 16);
						snprintf(keyName, EMU_MAX_CHAR_KEYNAME, "M%01X", 3+j);
						if(!SetKey('I', ident, keyName, newPmkKey, 16, 1)) {
							free(newPmkKey);
						}
						(*keysAdded)++;
						cs_hexdump(0, &emm[i+3+j*16], 16, keyValue, sizeof(keyValue));
						cs_log("[Emu] Key found in EMM: I %06X %s %s", ident, keyName, keyValue);
					}
				}
			}
			i+=l;
		}

		if(*keysAdded > 0) {
			return 0;
		}
	}

	return 1;
}

static const uint8_t fausto_xor[16] = { 0x22, 0x58, 0xBD, 0x85, 0x2E, 0x8E, 0x52, 0x80, 0xA3, 0x79, 0x98, 0x69, 0x68, 0xE2, 0xD8, 0x4D };

static int8_t Irdeto2EMM(uint16_t caid, uint8_t *oemm, uint32_t *keysAdded)
{
	uint8_t length, okeySeed[16], keySeed[16], keyIV[16], keyPMK[16], startOffset, emmType;
	uint32_t ident;
	uint8_t keySeedRef, keyIVRef, keyPMK0Ref, keyPMK1Ref, keyPMK0ERef, keyPMK1ERef;
	uint8_t emmCopy[EMU_MAX_EMM_LEN], *emm = oemm;
	uint16_t emmLen = GetEcmLen(emm);

	if(emmLen < 11) {
		return 1;
	}

	if(emm[3] == 0xC3 || emm[3] == 0xCB) {
		emmType = 2;
		startOffset = 11;
	}
	else {
		emmType = 1;
		startOffset = 10;
	}

	ident = emm[startOffset-2] | caid << 8;
	length = emm[startOffset-1];


	if(emmLen < length+startOffset) {
		return 1;
	}

	keySeedRef = 0;
	while(GetIrdetoKey(okeySeed, ident, 'M', emmType == 1 ? 0 : 0xA, 1, &keySeedRef)) {
		keyIVRef = 0;
		while(GetIrdetoKey(keyIV, ident, 'M', 2, 1, &keyIVRef)) {

			keyPMK0Ref = 0;
			keyPMK1Ref = 0;
			keyPMK0ERef = 0;
			keyPMK1ERef = 0;

			while(GetIrdetoKey(keyPMK, ident, 'M', emmType == 1 ? 3 : 0xB, 1, &keyPMK0Ref)) {
				memcpy(keySeed, okeySeed, 16);
				memcpy(emmCopy, oemm, emmLen);
				emm = emmCopy;
				if(emmType == 1) {
					if(Irdeto2DoEMMTypeOP(ident, emm, keySeed, keyIV, keyPMK, emmLen, startOffset, length, keysAdded) == 0) {
						return 0;
					}
				}
				else {
					if(Irdeto2DoEMMTypePMK(ident, emm, keySeed, keyIV, keyPMK, emmLen, startOffset, length, keysAdded) == 0) {
						return 0;
					}
				}
			}

			if(emmType == 1) {
				while(GetIrdetoKey(keyPMK, ident, 'M', 4, 1, &keyPMK1Ref)) {
					memcpy(keySeed, okeySeed, 16);
					memcpy(emmCopy, oemm, emmLen);
					emm = emmCopy;
					if(Irdeto2DoEMMTypeOP(ident, emm, keySeed, keyIV, keyPMK, emmLen, startOffset, length, keysAdded) == 0) {
						return 0;
					}
				}

				while(GetIrdetoKey(keyPMK, ident, 'M', 5, 1, &keyPMK0ERef)) {
					xxor(keyPMK, 16, keyPMK, fausto_xor);
					memcpy(keySeed, okeySeed, 16);
					memcpy(emmCopy, oemm, emmLen);
					emm = emmCopy;
					if(Irdeto2DoEMMTypeOP(ident, emm, keySeed, keyIV, keyPMK, emmLen, startOffset, length, keysAdded) == 0) {
						return 0;
					}
				}

				while(GetIrdetoKey(keyPMK, ident, 'M', 6, 1, &keyPMK1ERef)) {
					xxor(keyPMK, 16, keyPMK, fausto_xor);
					memcpy(keySeed, okeySeed, 16);
					memcpy(emmCopy, oemm, emmLen);
					emm = emmCopy;
					if(Irdeto2DoEMMTypeOP(ident, emm, keySeed, keyIV, keyPMK, emmLen, startOffset, length, keysAdded) == 0) {
						return 0;
					}
				}
			}

			if(keyPMK0Ref == 0 && keyPMK1Ref == 0 && keyPMK0ERef == 0 && keyPMK1ERef == 0) {
				return 2;
			}
		}
		if(keyIVRef == 0) {
			return 2;
		}
	}
	if(keySeedRef == 0) {
		return 2;
	}

	return 1;
}

int32_t GetIrdeto2Hexserial(uint16_t caid, uint8_t *hexserial)
{
	uint32_t i;
	KeyDataContainer *KeyDB;
	KeyData *tmpKeyData;

	KeyDB = GetKeyContainer('I');
	if(KeyDB == NULL) {
		return 0;
	}

	for(i=0; i<KeyDB->keyCount; i++) {

		if(KeyDB->EmuKeys[i].provider>>8 != caid) {
			continue;
		}
		if(strcmp(KeyDB->EmuKeys[i].keyName, "MC")) {
			continue;
		}

		tmpKeyData = &KeyDB->EmuKeys[i];

		memcpy(hexserial, tmpKeyData->key, tmpKeyData->keyLength > 2 ? 3 : tmpKeyData->keyLength);
		return 1;
	}

	return 0;
}

// PowerVu EMM EMU
static int8_t PowervuEMM(uint8_t *emm, uint32_t *keysAdded)
{
	uint8_t emmInfo, emmType, *newEcmKey;
	uint16_t emmLen = GetEcmLen(emm);
	uint32_t i, emmCrc32, uniqueAddress, channelId;
	uint8_t emmKey[7], tmpEmmKey[7];
	char keyName[EMU_MAX_CHAR_KEYNAME], keyValue[16];

	if(emmLen < 50)
	{
		return 1;
	}

	emmCrc32 = b2i(4, emm+emmLen-4);

	if(fletcher_crc32(emm, emmLen-4) != emmCrc32)
	{
		return 8;
	}
	emmLen -= 4;

	uniqueAddress = b2i(4, emm+12);

	snprintf(keyName, EMU_MAX_CHAR_KEYNAME, "%.8X", uniqueAddress);
	if(!GetPowervuEmmKey(emmKey, 0, keyName, 7, 1, &channelId))
	{
		return 0;
	}

	for(i=19; i+27<=emmLen; i+=27) {
		emmInfo = emm[i];

		if(!GetBit(emmInfo, 7))
		{
			continue;
		}

		//keyNb = emm[i] & 0x0F;

		memcpy(tmpEmmKey, emmKey, 7);
		PowervuDecrypt(emm+i+1, 26, tmpEmmKey);

		if((emm[13] != emm[i+24]) || (emm[14] != emm[i+25]) || (emm[15] != emm[i+26]))
		{
			continue;
		}

		emmType = emm[i+2] & 0x7F;
		if(emmType > 1)
		{
			continue;
		}

		newEcmKey = (uint8_t*)malloc(sizeof(uint8_t)*7);
		if(newEcmKey == NULL) {
			return 7;
		}
		memcpy(newEcmKey, &emm[i+3], 7);
		snprintf(keyName, EMU_MAX_CHAR_KEYNAME, "%.2X", emmType);
		if(!UpdateKey('P', channelId, keyName, newEcmKey, 7)) {
			free(newEcmKey);
		}
		(*keysAdded)++;
		cs_hexdump(0, &emm[i+3], 7, keyValue, sizeof(keyValue));
		cs_log("[Emu] Key found in EMM: P %.4X %s %s", channelId, keyName, keyValue);
	}

	return 0;
}

char* GetProcessEMMErrorReason(int8_t result)
{
	switch(result) {
	case 0:
		return "No error";
	case 1:
		return "EMM not supported";
	case 2:
		return "Key not found";
	case 3:
		return "Nano80 problem";
	case 4:
		return "Corrupt data";
	case 5:
		return "Unknown";
	case 6:
		return "Checksum error";
	case 7:
		return "Out of memory";
	case 8:
		return "EMM checksum error";
	default:
		return "Unknown";
	}
}

/* Error codes
0  OK
1  EMM not supported
2  Key not found
3  Nano80 problem
4  Corrupt data
5
6  Checksum error
7  Out of memory
*/
int8_t ProcessEMM(uint16_t caid, uint32_t UNUSED(provider), const uint8_t *emm, uint32_t *keysAdded)
{
	int8_t result = 1;
	uint8_t emmCopy[EMU_MAX_EMM_LEN];
	uint16_t emmLen = GetEcmLen(emm);

	if(emmLen > EMU_MAX_EMM_LEN) {
		return 1;
	}
	memcpy(emmCopy, emm, emmLen);
	*keysAdded = 0;

	if(caid==0x0500) {
		result = ViaccessEMM(emmCopy, keysAdded);
	}
	else if((caid>>8)==0x06) {
		result = Irdeto2EMM(caid, emmCopy, keysAdded);
	}
	else if((caid>>8)==0x0E) {
		result = PowervuEMM(emmCopy, keysAdded);
	}

	if(result != 0) {
		cs_log("[Emu] EMM failed: %s", GetProcessEMMErrorReason(result));
	}

	return result;
}
