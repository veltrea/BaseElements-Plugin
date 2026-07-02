/*
 BERegularExpression.h
 BaseElements Plug-In
 
 Copyright 2015-2021 Goya. All rights reserved.
 For conditions of distribution and use please see the copyright notice in BEPlugin.cpp
 
 http://www.goya.com.au/baseelements/plugin
 
 */


#ifndef BaseElements_BERegularExpression_h
#define BaseElements_BERegularExpression_h


#include "BEPluginException.h"

#include <Poco/Exception.h>
#include <Poco/RegularExpression.h>
#include <Poco/String.h>

#include <vector>



/*
 notes
 
 the options are a string consisting of, in any order
 i	case insensitive
 m	multiline
 s	dot matches all characters, including newline
 x	ignore whitespace
 g	replace/match all occurances
 
 if the replaceString parameter is present ( can be empty ) then a replace is performed
 otherwise a find
 
 */


template <typename T>
T regular_expression ( const T& text, const T& expression, const std::string options = "", const T& replace_with = "", const bool replace = false )
{

	// text from FileMaker is always UTF-8; without RE_UTF8 PCRE matches single bytes inside multi-byte characters
	int constructor_options = Poco::RegularExpression::RE_UTF8;
    int runtime_options = Poco::RegularExpression::RE_NOTEMPTY; // poco.re hangs when the expression evalutes as "empty" unless this is set;
    
    T regex_options = options;
    Poco::toLowerInPlace ( regex_options );
    
    std::size_t found = regex_options.find ( "i" );
    if ( found != std::string::npos ) {
        constructor_options |= Poco::RegularExpression::RE_CASELESS;
    }

    found = regex_options.find ( "m" );
    if ( found != std::string::npos ) {
        constructor_options |= Poco::RegularExpression::RE_MULTILINE;
    }
        
    found = regex_options.find ( "s" );
    if ( found != std::string::npos ) {
        constructor_options |= Poco::RegularExpression::RE_DOTALL;
    }
        
    found = regex_options.find ( "x" );
    if ( found != std::string::npos ) {
        constructor_options |= Poco::RegularExpression::RE_EXTENDED;
    }
        
    found = regex_options.find ( "g" );
    if ( found != std::string::npos ) {
        runtime_options |= Poco::RegularExpression::RE_GLOBAL;
    }
        
    T text_matched; // BE_ValueList doesn't work here (why?)

    try {
	
		Poco::RegularExpression re ( expression, constructor_options, false );
		
		if ( replace ) {

			// replace/substitute
			
			text_matched = text;
			re.subst ( text_matched, replace_with, runtime_options ); // int how_many =

		} else {
			
			if ( runtime_options & Poco::RegularExpression::RE_GLOBAL ) {
				
				// match all

				Poco::RegularExpression::Match match_details = { 0, 0 };
				std::string::size_type search_offset = 0;
				
				do {
					
					re.match ( text, search_offset, match_details, runtime_options );
					
					if ( std::string::npos != match_details.offset ) {
						
						if ( text_matched.length() > 0 ) {
							text_matched += FILEMAKER_END_OF_LINE;
						}
						
						text_matched += text.substr ( match_details.offset, match_details.length );
						search_offset = match_details.offset + match_details.length;
					}
				
				} while ( std::string::npos != match_details.offset );
				
			} else {

				// just the first match
				re.extract ( text, text_matched, runtime_options );

			}

		}

    } catch ( Poco::RegularExpressionException& e ) {
        // e.code() is 0 for pattern/UTF-8 errors; don't let that map to "no error"
        throw BEPlugin_Exception ( e.code() ? e.code() : kErrorUnknown );
    }

    return text_matched;

} // BE_RegularExpression


// split text on every match of a regular expression (UTF-8 aware)
// the trailing piece is always included, even when empty, to match
// the behaviour of boost::algorithm::split_regex which this replaces

inline std::vector<std::string> regular_expression_split ( const std::string& text, const std::string& expression )
{

	std::vector<std::string> values;

	try {

		Poco::RegularExpression re ( expression, Poco::RegularExpression::RE_UTF8, false );

		Poco::RegularExpression::Match match_details = { 0, 0 };
		std::string::size_type search_offset = 0;

		while ( true ) {

			re.match ( text, search_offset, match_details, Poco::RegularExpression::RE_NOTEMPTY );

			if ( std::string::npos == match_details.offset ) {
				break;
			}

			values.push_back ( text.substr ( search_offset, match_details.offset - search_offset ) );
			search_offset = match_details.offset + match_details.length;

		}

		values.push_back ( text.substr ( search_offset ) );

	} catch ( Poco::RegularExpressionException& e ) {
		throw BEPlugin_Exception ( e.code() ? e.code() : kErrorUnknown );
	}

	return values;

} // regular_expression_split


#endif
