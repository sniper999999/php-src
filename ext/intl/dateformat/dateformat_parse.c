/*
   +----------------------------------------------------------------------+
   | PHP Version 5                                                        |
   +----------------------------------------------------------------------+
   | This source file is subject to version 3.01 of the PHP license,      |
   | that is bundled with this package in the file LICENSE, and is        |
   | available through the world-wide-web at the following url:           |
   | http://www.php.net/license/3_01.txt                                  |
   | If you did not receive a copy of the PHP license and are unable to   |
   | obtain it through the world-wide-web, please send a note to          |
   | license@php.net so we can mail you a copy immediately.               |
   +----------------------------------------------------------------------+
   | Authors: Kirti Velankar <kirtig@yahoo-inc.com>                       |
   +----------------------------------------------------------------------+
*/

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include <unicode/ustring.h>
#include <math.h>

#include "php_intl.h"
#include "intl_convert.h"
#include "dateformat.h"
#include "dateformat_class.h"
#include "dateformat_parse.h"
#include "dateformat_data.h"

/* {{{ 
 * Internal function which calls the udat_parse
 * param int store_error acts like a boolean 
 *	if set to 1 - store any error encountered  in the parameter parse_error  
 *	if set to 0 - no need to store any error encountered  in the parameter parse_error  
*/
static void internal_parse_to_timestamp(IntlDateFormatter_object *dfo, char* text_to_parse, int32_t text_len, int32_t *parse_pos, zval *return_value TSRMLS_DC)
{
	double	result =  0;
	UDate 	timestamp   =0;
	UChar* 	text_utf16  = NULL;
	zend_str_size_int text_utf16_len = 0;

	/* Convert timezone to UTF-16. */
	intl_convert_utf8_to_utf16(&text_utf16, &text_utf16_len, text_to_parse, text_len, &INTL_DATA_ERROR_CODE(dfo));
	INTL_METHOD_CHECK_STATUS(dfo, "Error converting timezone to UTF-16" );

	timestamp = udat_parse( DATE_FORMAT_OBJECT(dfo), text_utf16, text_utf16_len, parse_pos, &INTL_DATA_ERROR_CODE(dfo));
	if( text_utf16 ){
		efree(text_utf16);
	}

	INTL_METHOD_CHECK_STATUS( dfo, "Date parsing failed" );
	
	/* Since return is in  sec. */
	result = (double)timestamp / U_MILLIS_PER_SECOND;
	if(result > LONG_MAX || result < -LONG_MAX) {
		ZVAL_DOUBLE(return_value, result<0?ceil(result):floor(result));
	} else {
		ZVAL_LONG(return_value, (php_int_t)result);
	}
}
/* }}} */

static void add_to_localtime_arr( IntlDateFormatter_object *dfo, zval* return_value, const UCalendar *parsed_calendar, php_int_t calendar_field, char* key_name TSRMLS_DC)
{
	php_int_t calendar_field_val = ucal_get( parsed_calendar, calendar_field, &INTL_DATA_ERROR_CODE(dfo));	
	INTL_METHOD_CHECK_STATUS( dfo, "Date parsing - localtime failed : could not get a field from calendar" );

	if( strcmp(key_name, CALENDAR_YEAR )==0 ){
		/* since tm_year is years from 1900 */
		add_assoc_long( return_value, key_name,( calendar_field_val-1900) ); 
	}else if( strcmp(key_name, CALENDAR_WDAY )==0 ){
		/* since tm_wday starts from 0 whereas ICU WDAY start from 1 */
		add_assoc_long( return_value, key_name,( calendar_field_val-1) ); 
	}else{
		add_assoc_long( return_value, key_name, calendar_field_val ); 
	}
}

/* {{{
 * Internal function which calls the udat_parseCalendar
*/
static void internal_parse_to_localtime(IntlDateFormatter_object *dfo, char* text_to_parse, int32_t text_len, int32_t *parse_pos, zval *return_value TSRMLS_DC)
{
	UCalendar      *parsed_calendar = NULL;
	UChar*  	text_utf16  = NULL;
	zend_str_size_int 	text_utf16_len = 0;
	php_int_t 		isInDST = 0;

	/* Convert timezone to UTF-16. */
	intl_convert_utf8_to_utf16(&text_utf16, &text_utf16_len, text_to_parse, text_len, &INTL_DATA_ERROR_CODE(dfo));
	INTL_METHOD_CHECK_STATUS(dfo, "Error converting timezone to UTF-16" );

	parsed_calendar = (UCalendar *)udat_getCalendar(DATE_FORMAT_OBJECT(dfo));
	udat_parseCalendar( DATE_FORMAT_OBJECT(dfo), parsed_calendar, text_utf16, text_utf16_len, parse_pos, &INTL_DATA_ERROR_CODE(dfo));
	
	if (text_utf16) {
		efree(text_utf16);
	}

	INTL_METHOD_CHECK_STATUS( dfo, "Date parsing failed" );


	array_init( return_value );
	/* Add  entries from various fields of the obtained parsed_calendar */
	add_to_localtime_arr( dfo, return_value, parsed_calendar, UCAL_SECOND, CALENDAR_SEC TSRMLS_CC);
	add_to_localtime_arr( dfo, return_value, parsed_calendar, UCAL_MINUTE, CALENDAR_MIN TSRMLS_CC);
	add_to_localtime_arr( dfo, return_value, parsed_calendar, UCAL_HOUR_OF_DAY, CALENDAR_HOUR TSRMLS_CC);
	add_to_localtime_arr( dfo, return_value, parsed_calendar, UCAL_YEAR, CALENDAR_YEAR TSRMLS_CC); 
	add_to_localtime_arr( dfo, return_value, parsed_calendar, UCAL_DAY_OF_MONTH, CALENDAR_MDAY TSRMLS_CC);
	add_to_localtime_arr( dfo, return_value, parsed_calendar, UCAL_DAY_OF_WEEK, CALENDAR_WDAY TSRMLS_CC);
	add_to_localtime_arr( dfo, return_value, parsed_calendar, UCAL_DAY_OF_YEAR, CALENDAR_YDAY TSRMLS_CC);
	add_to_localtime_arr( dfo, return_value, parsed_calendar, UCAL_MONTH, CALENDAR_MON TSRMLS_CC);

	/* Is in DST? */
	isInDST = ucal_inDaylightTime(parsed_calendar	, &INTL_DATA_ERROR_CODE(dfo));
	INTL_METHOD_CHECK_STATUS( dfo, "Date parsing - localtime failed : while checking if currently in DST." );
	add_assoc_long( return_value, CALENDAR_ISDST,(isInDST==1?1:0)); 
}
/* }}} */


/* {{{ proto integer IntlDateFormatter::parse( string $text_to_parse  [, int $parse_pos] )
 * Parse the string $value starting at parse_pos to a Unix timestamp -int }}}*/
/* {{{ proto integer datefmt_parse( IntlDateFormatter $fmt, string $text_to_parse [, int $parse_pos] )
 * Parse the string $value starting at parse_pos to a Unix timestamp -int }}}*/
PHP_FUNCTION(datefmt_parse)
{
	char*           text_to_parse = NULL;
	zend_str_size_int         text_len =0;
	zval*         	z_parse_pos = NULL;
	int32_t			parse_pos = -1;

	DATE_FORMAT_METHOD_INIT_VARS;

	/* Parse parameters. */
	if( zend_parse_method_parameters( ZEND_NUM_ARGS() TSRMLS_CC, getThis(), "OS|z!",
		&object, IntlDateFormatter_ce_ptr, &text_to_parse, &text_len, &z_parse_pos ) == FAILURE ){
		intl_error_set( NULL, U_ILLEGAL_ARGUMENT_ERROR, "datefmt_parse: unable to parse input params", 0 TSRMLS_CC );
		RETURN_FALSE;
	}

	/* Fetch the object. */
	DATE_FORMAT_METHOD_FETCH_OBJECT;

	if (z_parse_pos) {
		convert_to_long(z_parse_pos);
		parse_pos = (int32_t)Z_LVAL_P(z_parse_pos);
		if(parse_pos > text_len) {
			RETURN_FALSE;
		}
	}
	internal_parse_to_timestamp( dfo, text_to_parse, text_len, z_parse_pos?&parse_pos:NULL, return_value TSRMLS_CC);
	if(z_parse_pos) {
		zval_dtor(z_parse_pos);
		ZVAL_LONG(z_parse_pos, parse_pos);
	}
}
/* }}} */

/* {{{ proto integer IntlDateFormatter::localtime( string $text_to_parse[, int $parse_pos] )
 * Parse the string $value to a localtime array  }}}*/
/* {{{ proto integer datefmt_localtime( IntlDateFormatter $fmt, string $text_to_parse[, int $parse_pos ])
 * Parse the string $value to a localtime array  }}}*/
PHP_FUNCTION(datefmt_localtime)
{
	char*           text_to_parse = NULL;
	zend_str_size_int         text_len =0;
	zval*         	z_parse_pos = NULL;
	int32_t			parse_pos = -1;

	DATE_FORMAT_METHOD_INIT_VARS;

	/* Parse parameters. */
	if( zend_parse_method_parameters( ZEND_NUM_ARGS() TSRMLS_CC, getThis(), "OS|z!",
		&object, IntlDateFormatter_ce_ptr, &text_to_parse, &text_len, &z_parse_pos ) == FAILURE ){
		intl_error_set( NULL, U_ILLEGAL_ARGUMENT_ERROR, "datefmt_parse_to_localtime: unable to parse input params", 0 TSRMLS_CC );
		RETURN_FALSE;
	}

    /* Fetch the object. */
	DATE_FORMAT_METHOD_FETCH_OBJECT;

	if(z_parse_pos) {
		convert_to_long(z_parse_pos);
		parse_pos = (int32_t)Z_LVAL_P(z_parse_pos);
		if(parse_pos > text_len) {
			RETURN_FALSE;
		}
	}
	internal_parse_to_localtime( dfo, text_to_parse, text_len, z_parse_pos?&parse_pos:NULL, return_value TSRMLS_CC);
	if(z_parse_pos) {
		zval_dtor(z_parse_pos);
		ZVAL_LONG(z_parse_pos, parse_pos);
	}
}
/* }}} */

