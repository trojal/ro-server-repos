// Copyright (c) Athena Dev Teams - Licensed under GNU GPL
// For more information, see LICENCE in the main folder

#include "../common/cbasetypes.h"
#include "../common/malloc.h"
#include "strlib.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>



#define J_MAX_MALLOC_SIZE 65535

// escapes a string in-place (' -> \' , \ -> \\ , % -> _)
char* jstrescape (char* pt)
{
	//copy from here
	char *ptr;
	int i = 0, j = 0;

	//copy string to temporary
	CREATE(ptr, char, J_MAX_MALLOC_SIZE);
	strcpy(ptr,pt);

	while (ptr[i] != '\0') {
		switch (ptr[i]) {
			case '\'':
				pt[j++] = '\\';
				pt[j++] = ptr[i++];
				break;
			case '\\':
				pt[j++] = '\\';
				pt[j++] = ptr[i++];
				break;
			case '%':
				pt[j++] = '_'; i++;
				break;
			default:
				pt[j++] = ptr[i++];
		}
	}
	pt[j++] = '\0';
	aFree(ptr);
	return pt;
}

// escapes a string into a provided buffer
char* jstrescapecpy (char* pt, const char* spt)
{
	//copy from here
	//WARNING: Target string pt should be able to hold strlen(spt)*2, as each time
	//a escape character is found, the target's final length increases! [Skotlex]
	int i =0, j=0;

	if (!spt) {	//Return an empty string [Skotlex]
		pt[0] = '\0';
		return &pt[0];
	}
	
	while (spt[i] != '\0') {
		switch (spt[i]) {
			case '\'':
				pt[j++] = '\\';
				pt[j++] = spt[i++];
				break;
			case '\\':
				pt[j++] = '\\';
				pt[j++] = spt[i++];
				break;
			case '%':
				pt[j++] = '_'; i++;
				break;
			default:
				pt[j++] = spt[i++];
		}
	}
	pt[j++] = '\0';
	return &pt[0];
}

// escapes exactly 'size' bytes of a string into a provided buffer
int jmemescapecpy (char* pt, const char* spt, int size)
{
	//copy from here
	int i =0, j=0;

	while (i < size) {
		switch (spt[i]) {
			case '\'':
				pt[j++] = '\\';
				pt[j++] = spt[i++];
				break;
			case '\\':
				pt[j++] = '\\';
				pt[j++] = spt[i++];
				break;
			case '%':
				pt[j++] = '_'; i++;
				break;
			default:
				pt[j++] = spt[i++];
		}
	}
	// copy size is 0 ~ (j-1)
	return j;
}

// Function to suppress control characters in a string.
int remove_control_chars(char* str)
{
	int i;
	int change = 0;

	for(i = 0; str[i]; i++) {
		if (ISCNTRL(str[i])) {
			str[i] = '_';
			change = 1;
		}
	}

	return change;
}

// Removes characters identified by ISSPACE from the start and end of the string
// NOTE: make sure the string is not const!!
char* trim(char* str)
{
	size_t start;
	size_t end;

	if( str == NULL )
		return str;

	// get start position
	for( start = 0; str[start] && ISSPACE(str[start]); ++start )
		;
	// get end position
	for( end = strlen(str); start < end && str[end-1] && ISSPACE(str[end-1]); --end )
		;
	// trim
	if( start == end )
		*str = '\0';// empty string
	else
	{// move string with nul terminator
		str[end] = '\0';
		memmove(str,str+start,end-start+1);
	}
	return str;
}

// Converts one or more consecutive occurences of the delimiters into a single space
// and removes such occurences from the beginning and end of string
// NOTE: make sure the string is not const!!
char* normalize_name(char* str,const char* delims)
{
	char* in = str;
	char* out = str;
	int put_space = 0;

	if( str == NULL || delims == NULL )
		return str;

	// trim start of string
	while( *in && strchr(delims,*in) )
		++in;
	while( *in )
	{
		if( put_space )
		{// replace trim characters with a single space
			*out = ' ';
			++out;
		}
		// copy non trim characters
		while( *in && !strchr(delims,*in) )
		{
			*out = *in;
			++out;
			++in;
		}
		// skip trim characters
		while( *in && strchr(delims,*in) )
			++in;
		put_space = 1;
	}
	*out = '\0';
	return str;
}

//stristr: Case insensitive version of strstr, code taken from 
//http://www.daniweb.com/code/snippet313.html, Dave Sinkula
//
const char* stristr(const char* haystack, const char* needle)
{
	if ( !*needle )
	{
		return haystack;
	}
	for ( ; *haystack; ++haystack )
	{
		if ( TOUPPER(*haystack) == TOUPPER(*needle) )
		{
			// matched starting char -- loop through remaining chars
			const char *h, *n;
			for ( h = haystack, n = needle; *h && *n; ++h, ++n )
			{
				if ( TOUPPER(*h) != TOUPPER(*n) )
				{
					break;
				}
			}
			if ( !*n ) // matched all of 'needle' to null termination
			{
				return haystack; // return the start of the match
			}
		}
	}
	return 0;
}

#ifdef __WIN32
char* _strtok_r(char *s1, const char *s2, char **lasts)
{
	char *ret;

	if (s1 == NULL)
		s1 = *lasts;
	while(*s1 && strchr(s2, *s1))
		++s1;
	if(*s1 == '\0')
		return NULL;
	ret = s1;
	while(*s1 && !strchr(s2, *s1))
		++s1;
	if(*s1)
		*s1++ = '\0';
	*lasts = s1;
	return ret;
}
#endif

#if !(defined(WIN32) && defined(_MSC_VER) && _MSC_VER >= 1400)
/* Find the length of STRING, but scan at most MAXLEN characters.
   If no '\0' terminator is found in that many characters, return MAXLEN.  */
size_t strnlen (const char* string, size_t maxlen)
{
  const char* end = memchr (string, '\0', maxlen);
  return end ? (size_t) (end - string) : maxlen;
}
#endif

//----------------------------------------------------
// E-mail check: return 0 (not correct) or 1 (valid).
//----------------------------------------------------
int e_mail_check(char* email)
{
	char ch;
	char* last_arobas;
	size_t len = strlen(email);

	// athena limits
	if (len < 3 || len > 39)
		return 0;

	// part of RFC limits (official reference of e-mail description)
	if (strchr(email, '@') == NULL || email[len-1] == '@')
		return 0;

	if (email[len-1] == '.')
		return 0;

	last_arobas = strrchr(email, '@');

	if (strstr(last_arobas, "@.") != NULL || strstr(last_arobas, "..") != NULL)
		return 0;

	for(ch = 1; ch < 32; ch++)
		if (strchr(last_arobas, ch) != NULL)
			return 0;

	if (strchr(last_arobas, ' ') != NULL || strchr(last_arobas, ';') != NULL)
		return 0;

	// all correct
	return 1;
}

//--------------------------------------------------
// Return numerical value of a switch configuration
// on/off, english, fran�ais, deutsch, espa�ol
//--------------------------------------------------
int config_switch(const char* str)
{
	if (strcmpi(str, "on") == 0 || strcmpi(str, "yes") == 0 || strcmpi(str, "oui") == 0 || strcmpi(str, "ja") == 0 || strcmpi(str, "si") == 0)
		return 1;
	if (strcmpi(str, "off") == 0 || strcmpi(str, "no") == 0 || strcmpi(str, "non") == 0 || strcmpi(str, "nein") == 0)
		return 0;

	return (int)strtol(str, NULL, 0);
}

/// always nul-terminates the string
char* safestrncpy(char* dst, const char* src, size_t n)
{
	char* ret;
	ret = strncpy(dst, src, n);
	if( ret != NULL )
		ret[n - 1] = '\0';
	return ret;
}

/// doesn't crash on null pointer
size_t safestrnlen(const char* string, size_t maxlen)
{
	return ( string != NULL ) ? strnlen(string, maxlen) : 0;
}


/////////////////////////////////////////////////////////////////////
// StringBuf - dynamic string
//
// @author MouseJstr (original)

/// Allocates a StringBuf
StringBuf* StringBuf_Malloc() 
{
	StringBuf* self;
	CREATE(self, StringBuf, 1);
	StringBuf_Init(self);
	return self;
}

/// Initializes a previously allocated StringBuf
void StringBuf_Init(StringBuf* self)
{
	self->max_ = 1024;
	self->ptr_ = self->buf_ = (char*)aMallocA(self->max_ + 1);
}

/// Appends the result of printf to the StringBuf
int StringBuf_Printf(StringBuf* self, const char* fmt, ...)
{
	int len;
	va_list ap;

	va_start(ap, fmt);
	len = StringBuf_Vprintf(self, fmt, ap);
	va_end(ap);

	return len;
}

/// Appends the result of vprintf to the StringBuf
int StringBuf_Vprintf(StringBuf* self, const char* fmt, va_list ap)
{
	int n, size, off;

	for(;;)
	{
		/* Try to print in the allocated space. */
		size = self->max_ - (self->ptr_ - self->buf_);
		n = vsnprintf(self->ptr_, size, fmt, ap);
		/* If that worked, return the length. */
		if( n > -1 && n < size )
		{
			self->ptr_ += n;
			return (int)(self->ptr_ - self->buf_);
		}
		/* Else try again with more space. */
		self->max_ *= 2; // twice the old size
		off = (int)(self->ptr_ - self->buf_);
		self->buf_ = (char*)aRealloc(self->buf_, self->max_ + 1);
		self->ptr_ = self->buf_ + off;
	}
}

/// Appends the contents of another StringBuf to the StringBuf
int StringBuf_Append(StringBuf* self, const StringBuf* sbuf)
{
	int available = self->max_ - (self->ptr_ - self->buf_);
	int needed = (int)(sbuf->ptr_ - sbuf->buf_);

	if( needed >= available )
	{
		int off = (int)(self->ptr_ - self->buf_);
		self->max_ += needed;
		self->buf_ = (char*)aRealloc(self->buf_, self->max_ + 1);
		self->ptr_ = self->buf_ + off;
	}

	memcpy(self->ptr_, sbuf->buf_, needed);
	self->ptr_ += needed;
	return (int)(self->ptr_ - self->buf_);
}

// Appends str to the StringBuf
int StringBuf_AppendStr(StringBuf* self, const char* str) 
{
	int available = self->max_ - (self->ptr_ - self->buf_);
	int needed = (int)strlen(str);

	if( needed >= available )
	{// not enough space, expand the buffer (minimum expansion = 1024)
		int off = (int)(self->ptr_ - self->buf_);
		self->max_ += max(needed, 1024);
		self->buf_ = (char*)aRealloc(self->buf_, self->max_ + 1);
		self->ptr_ = self->buf_ + off;
	}

	memcpy(self->ptr_, str, needed);
	self->ptr_ += needed;
	return (int)(self->ptr_ - self->buf_);
}

// Returns the length of the data in the Stringbuf
int StringBuf_Length(StringBuf* self) 
{
	return (int)(self->ptr_ - self->buf_);
}

/// Returns the data in the StringBuf
char* StringBuf_Value(StringBuf* self) 
{
	*self->ptr_ = '\0';
	return self->buf_;
}

/// Clears the contents of the StringBuf
void StringBuf_Clear(StringBuf* self) 
{
	self->ptr_ = self->buf_;
}

/// Destroys the StringBuf
void StringBuf_Destroy(StringBuf* self)
{
	aFree(self->buf_);
	self->ptr_ = self->buf_ = 0;
	self->max_ = 0;
}

// Frees a StringBuf returned by StringBuf_Malloc
void StringBuf_Free(StringBuf* self) 
{
	StringBuf_Destroy(self);
	aFree(self);
}
