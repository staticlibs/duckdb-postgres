#include "duckdb/common/vector/list_vector.hpp"
#include "duckdb/common/vector/map_vector.hpp"
#include "duckdb/common/vector/string_vector.hpp"
#include "duckdb/common/vector/struct_vector.hpp"
#include "postgres_text_reader.hpp"
#include "postgres_binary_parser.hpp"
#include "postgres_scanner.hpp"
#include "duckdb/common/types/blob.hpp"

namespace duckdb {

PostgresTextReader::PostgresTextReader(ClientContext &context, PostgresConnection &con_p,
                                       const vector<column_t> &column_ids, const PostgresBindData &bind_data)
    : PostgresResultReader(con_p, column_ids, bind_data), context(context) {
}

PostgresTextReader::~PostgresTextReader() {
	Reset();
}

void PostgresTextReader::BeginCopy(ClientContext &context, const string &sql) {
	// Request results in binary format by passing format=1 to the Query call
	result = con.Query(context, sql, bind_data.params, 1);
	row_offset = 0;
}

void PostgresTextReader::ConvertVector(Vector &source, Vector &target, const PostgresType &postgres_type, idx_t count) {
	throw InternalException("PostgresTextReader::ConvertVector is deprecated");
}

PostgresReadResult PostgresTextReader::Read(DataChunk &output) {
	if (!result) {
		return PostgresReadResult::FINISHED;
	}

	// We use the binary parser logic to process binary results directly from the result set
	PostgresBinaryParser parser(bind_data.types, bind_data.postgres_types);

	output.Reset();
	for (; output.size() < STANDARD_VECTOR_SIZE && row_offset < result->Count(); row_offset++) {
		idx_t output_offset = output.size();
		for (idx_t output_idx = 0; output_idx < output.ColumnCount(); output_idx++) {
			auto col_idx = column_ids[output_idx];
			auto &out_vec = output.data[output_idx];

			if (result->IsNull(row_offset, output_idx)) {
				FlatVector::SetNull(out_vec, output_offset, true);
				continue;
			}

			// Obtain the raw binary data from the result cell
			auto val = result->GetStringRef(row_offset, output_idx);
			parser.SetBuffer(data_ptr_cast(val.GetDataWriteable()), val.GetSize());

			if (col_idx == COLUMN_IDENTIFIER_ROW_ID) {
				PostgresType ctid_type;
				ctid_type.info = PostgresTypeAnnotation::CTID;
				parser.ParseValue(LogicalType::BIGINT, ctid_type, NumericCast<int32_t>(val.GetSize()), out_vec,
				                  output_offset);
			} else {
				parser.ParseValue(bind_data.types[col_idx], bind_data.postgres_types[col_idx],
				                  NumericCast<int32_t>(val.GetSize()), out_vec, output_offset);
			}
		}
		output.SetChildCardinality(output_offset + 1);
	}

	bool finished = row_offset >= result->Count();
	if (finished) {
		Reset();
		return PostgresReadResult::FINISHED;
	}
	return PostgresReadResult::HAVE_MORE_TUPLES;
}

void PostgresTextReader::Reset() {
	result.reset();
	row_offset = 0;
}

} // namespace duckdb
