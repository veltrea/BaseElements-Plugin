/*
 BESQLCommand.cpp
 BaseElements Plug-In

 Copyright 2011-2021 Goya. All rights reserved.
 For conditions of distribution and use please see the copyright notice in BEPlugin.cpp

 http://www.goya.com.au/baseelements/plugin

 */


#include "BESQLCommand.h"
#include "BEPluginGlobalDefines.h"
#include "BEPluginUtilities.h"


using namespace std;
using namespace fmx;


BESQLCommandUniquePtr g_ddl_command;


BESQLCommand::BESQLCommand ( const Text& _expression, const Text& _filename )
{
	expression->SetText ( _expression );
	filename->SetText ( _filename );
}


BESQLCommand::BESQLCommand ( const std::string& _expression, const std::string& _filename )
{
	expression->Assign ( _expression.c_str() );
	filename->Assign ( _filename.c_str() );
}


const fmx::errcode BESQLCommand::execute ( )
{
	ExprEnvUniquePtr environment;
	FMX_SetToCurrentEnv ( &(*environment) );
	return execute ( *environment );
}



const fmx::errcode BESQLCommand::execute ( const ExprEnv& environment, const bool text_result_wanted )
{
	bool ddl = is_ddl_command();

	if ( ddl && !waiting ) {

		// unique_ptrs : do not use the copy constructor
		BESQLCommandUniquePtr command ( new BESQLCommand ( *expression, *filename ) );
		command->wait();

#pragma TODO Remove global dependency
		
		g_ddl_command.swap ( command );

	} else {
		
		if ( text_result_wanted ) {

			if ( gFMX_ExternCallPtr->extnVersion >= k130ExtnVersion ) {

				error = environment.ExecuteFileSQLTextResult ( *expression, *filename, *parameters, *result, column_separator, row_separator );

			} else {

				// FM_ExprEnv_ExecuteFileSQLTextResult is not exported before FMP 13 (API 54)
				// assemble the text result from the row data (as the plug-in did prior to v3.3)

				RowVectUniquePtr records_found;
				error = environment.ExecuteFileSQL ( *expression, *filename, *parameters, *records_found );

				if ( error == kNoError ) {

					TextUniquePtr column_separator_text;
					if ( column_separator != '\0' ) {
						column_separator_text->AssignUnicodeWithLength ( &column_separator, 1 );
					}

					TextUniquePtr row_separator_text;
					if ( row_separator != '\0' ) {
						row_separator_text->AssignUnicodeWithLength ( &row_separator, 1 );
					}

					TextUniquePtr text_result;
					const FMX_UInt32 number_of_rows = records_found->Size();

					for ( FMX_UInt32 row = 0 ; row < number_of_rows ; ++row ) {

						const DataVect& this_row = records_found->At ( row );
						const FMX_UInt32 number_of_columns = this_row.Size();

						for ( FMX_UInt32 column = 0 ; column < number_of_columns ; ++column ) {

							text_result->AppendText ( this_row.At ( column ).GetAsText() );
							if ( column + 1 < number_of_columns ) {
								text_result->AppendText ( *column_separator_text );
							}

						}

						if ( row + 1 < number_of_rows ) {
							text_result->AppendText ( *row_separator_text );
						}

					}

					LocaleUniquePtr default_locale;
					result->SetAsText ( *text_result, *default_locale );

				}

			}

		} else {
		
			RowVectUniquePtr records_found;
			error = environment.ExecuteFileSQL ( *expression, *filename, *parameters, *records_found );
			auto rows = records_found->Size();
	
			if ( rows > 0 ) {
				
				auto& this_row = records_found->At(0);
				auto columns = this_row.Size();

				if ( columns == 0 ) {
					; // do nothing
				} else if ( rows == 1 &&  columns == 1 ) {
					
					// take the first field/column of the first record
					auto& container = this_row.At(0);
					if ( container.GetNativeType() == fmx::Data::kDTBinary ) {
						result->SetBinaryData ( container.GetBinaryData() );
					} else {
						error = kInvalidFieldType;
					}
					
				} else {
					error = kInvalidFieldType;
				}

			}

		} // if ( text_result_wanted )
		
		if ( ddl ) {
			ddl_error = error;
		}

	}
	
	return error;

} // execute



DataUniquePtr BESQLCommand::get_data_result ( void ) const
{
	DataUniquePtr out;
	out->SetBinaryData ( result->GetBinaryData() );
	return out;
}


TextUniquePtr BESQLCommand::get_text_result ( void )
{
	TextUniquePtr text_result;

	if ( !waiting ) {
		text_result->AppendText ( result->GetAsText() );
	}

	return text_result;

}


std::vector<char> BESQLCommand::get_vector_result ( void ) const
{
	std::vector<char> vector_result;

	if ( !waiting ) {
		vector_result = DataAsVectorChar ( *result );
	}

	return vector_result;

}


void BESQLCommand::set_column_separator ( const Text& new_column_separator )
{
	if ( new_column_separator.GetSize() >= 1 ) {
		new_column_separator.GetUnicode ( &column_separator, 0, 1 );
	} else {
		column_separator = '\0';
	}
}



void BESQLCommand::set_row_separator ( const Text& new_row_separator )
{
	if ( new_row_separator.GetSize() >= 1 ) {
		new_row_separator.GetUnicode ( &row_separator, 0, 1 );
	} else {
		row_separator = '\0';
	}
}



bool BESQLCommand::is_ddl_command ( void ) const
{
	const TextUniquePtr alter;
	alter->Assign ( "ALTER" );

	const TextUniquePtr create;
	create->Assign ( "CREATE" );

	const TextUniquePtr drop;
	drop->Assign ( "DROP" );

	bool is_ddl = expression->FindIgnoringCase ( *alter, 0 ) == 0 ||
				expression->FindIgnoringCase ( *create, 0 ) == 0 ||
				expression->FindIgnoringCase ( *drop, 0 ) == 0;

	return is_ddl;

}
