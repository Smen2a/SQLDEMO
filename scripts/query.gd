class_name Query
extends RefCounted

## A very small in-memory table with a chainable, SQL-flavoured query API.
## Rows are Dictionaries keyed by column name.

var columns: PackedStringArray
var rows: Array[Dictionary]

func _init(p_columns: PackedStringArray, p_rows: Array[Dictionary]) -> void:
	columns = p_columns
	rows = p_rows

## Keeps the rows for which `predicate` returns true.
func where(predicate: Callable) -> Query:
	var kept: Array[Dictionary] = []
	for row in rows:
		if predicate.call(row):
			kept.append(row)
	return Query.new(columns, kept)

## Sorts by a single column, ascending unless `descending` is set.
func order_by(column: String, descending: bool = false) -> Query:
	var sorted: Array[Dictionary] = rows.duplicate()
	sorted.sort_custom(func(a, b):
		if descending:
			return a[column] > b[column]
		return a[column] < b[column])
	return Query.new(columns, sorted)

func limit(count: int) -> Query:
	return Query.new(columns, rows.slice(0, count))

## Narrows the result to the given columns.
func select(p_columns: PackedStringArray) -> Query:
	var projected: Array[Dictionary] = []
	for row in rows:
		var narrowed: Dictionary = {}
		for column in p_columns:
			narrowed[column] = row[column]
		projected.append(narrowed)
	return Query.new(p_columns, projected)

## Renders the result as an ASCII table, the way a SQL shell would.
func to_ascii_table() -> String:
	var widths: Array[int] = []
	for column in columns:
		var width: int = column.length()
		for row in rows:
			width = maxi(width, _cell(row[column]).length())
		widths.append(width)

	var rule := "+"
	for width in widths:
		rule += "-".repeat(width + 2) + "+"

	var lines: PackedStringArray = [rule, _format_row(columns, widths), rule]
	for row in rows:
		var values: PackedStringArray = []
		for column in columns:
			values.append(_cell(row[column]))
		lines.append(_format_row(values, widths))
	lines.append(rule)
	lines.append("%d row%s in set" % [rows.size(), "" if rows.size() == 1 else "s"])
	return "\n".join(lines)

func _format_row(values: PackedStringArray, widths: Array[int]) -> String:
	var line := "|"
	for i in values.size():
		line += " " + values[i].rpad(widths[i]) + " |"
	return line

func _cell(value: Variant) -> String:
	if value is float:
		return "%.2f" % value
	return str(value)
