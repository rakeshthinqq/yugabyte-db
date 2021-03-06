//--------------------------------------------------------------------------------------------------
// Copyright (c) YugaByte, Inc.
//
// Licensed under the Apache License, Version 2.0 (the "License"); you may not use this file except
// in compliance with the License.  You may obtain a copy of the License at
//
// http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software distributed under the License
// is distributed on an "AS IS" BASIS, WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express
// or implied.  See the License for the specific language governing permissions and limitations
// under the License.
//
//--------------------------------------------------------------------------------------------------

#include "yb/yql/cql/ql/exec/executor.h"

namespace yb {
namespace ql {

using std::shared_ptr;

//--------------------------------------------------------------------------------------------------

CHECKED_STATUS Executor::ColumnRefsToPB(const PTDmlStmt *tnode,
                                        QLReferencedColumnsPB *columns_pb) {
  // Write a list of columns to be read before executing the statement.
  const MCSet<int32>& column_refs = tnode->column_refs();
  for (auto column_ref : column_refs) {
    columns_pb->add_ids(column_ref);
  }

  const MCSet<int32>& static_column_refs = tnode->static_column_refs();
  for (auto column_ref : static_column_refs) {
    columns_pb->add_static_ids(column_ref);
  }
  return Status::OK();
}

CHECKED_STATUS Executor::ColumnArgsToPB(const PTDmlStmt *tnode, QLWriteRequestPB *req) {
  const MCVector<ColumnArg>& column_args = tnode->column_args();

  for (const ColumnArg& col : column_args) {
    if (!col.IsInitialized()) {
      // This column is not assigned a value, ignore it. We don't support default value yet.
      continue;
    }

    const ColumnDesc *col_desc = col.desc();
    QLExpressionPB *expr_pb;

    VLOG(3) << "WRITE request, column id = " << col_desc->id();
    if (col_desc->is_hash()) {
      expr_pb = req->add_hashed_column_values();
    } else if (col_desc->is_primary()) {
      expr_pb = req->add_range_column_values();
    } else {
      QLColumnValuePB* col_pb = req->add_column_values();
      col_pb->set_column_id(col_desc->id());
      expr_pb = col_pb->mutable_expr();
    }

    RETURN_NOT_OK(PTExprToPB(col.expr(), expr_pb));
    if (col_desc->is_primary()) {
      RETURN_NOT_OK(EvalExpr(expr_pb, QLTableRow::empty_row()));
    }

    // Null values not allowed for primary key: checking here catches nulls introduced by bind.
    if (col_desc->is_primary() && expr_pb->has_value() && IsNull(expr_pb->value())) {
      LOG(INFO) << "Unexpected null value. Current request: " << req->DebugString();
      return exec_context().Error(ErrorCode::NULL_ARGUMENT_FOR_PRIMARY_KEY);
    }
  }

  const MCVector<SubscriptedColumnArg>& subcol_args = tnode->subscripted_col_args();
  for (const SubscriptedColumnArg& col : subcol_args) {
    const ColumnDesc *col_desc = col.desc();
    QLColumnValuePB *col_pb = req->add_column_values();
    col_pb->set_column_id(col_desc->id());
    QLExpressionPB *expr_pb = col_pb->mutable_expr();
    RETURN_NOT_OK(PTExprToPB(col.expr(), expr_pb));
    for (auto& col_arg : col.args()->node_list()) {
      QLExpressionPB *arg_pb = col_pb->add_subscript_args();
      RETURN_NOT_OK(PTExprToPB(col_arg, arg_pb));
    }
  }

  return Status::OK();
}

}  // namespace ql
}  // namespace yb
