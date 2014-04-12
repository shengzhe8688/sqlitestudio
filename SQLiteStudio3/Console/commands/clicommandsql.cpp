#include "clicommandsql.h"
#include "cli.h"
#include "parser/ast/sqliteselect.h"
#include "parser/parser.h"
#include "parser/parsererror.h"
#include "db/queryexecutor.h"
#include "qio.h"
#include "common/unused.h"
#include "cli_config.h"
#include "cliutils.h"
#include <QList>
#include <QDebug>

void CliCommandSql::execute()
{
    if (!cli->getCurrentDb())
    {
        println(tr("No working database is set.\n"
                   "Call %1 command to set working database.\n"
                   "Call %2 to see list of all databases.")
                .arg(cmdName("use")).arg(cmdName("dblist")));

        return;
    }

    Db* db = cli->getCurrentDb();
    if (!db || !db->isOpen())
    {
        println(tr("Database is not open."));
        return;
    }

    // Executor deletes itself later when called with lambda.
    QueryExecutor *executor = new QueryExecutor(db, syntax.getArgument(STRING));
    connect(executor, SIGNAL(executionFinished(SqlResultsPtr)), this, SIGNAL(execComplete()));
    connect(executor, SIGNAL(executionFailed(int,QString)), this, SLOT(executionFailed(int,QString)));
    connect(executor, SIGNAL(executionFailed(int,QString)), this, SIGNAL(execComplete()));

    executor->exec([=](SqlResultsPtr results)
    {
        if (results->isError())
            return; // should not happen, since results handler function is called only for successful executions

        switch (CFG_CLI.Console.ResultsDisplayMode.get())
        {
            case CliResultsDisplay::FIXED:
                printResultsFixed(executor, results);
                break;
            case CliResultsDisplay::COLUMNS:
                printResultsColumns(executor, results);
                break;
            case CliResultsDisplay::ROW:
                printResultsRowByRow(executor, results);
                break;
            default:
                printResultsClassic(executor, results);
                break;
        }
    });
}

QString CliCommandSql::shortHelp() const
{
    return tr("executes SQL query");
}

QString CliCommandSql::fullHelp() const
{
    return tr(
                "This command is executed every time you enter SQL query in command prompt. "
                "It executes the query on the current working database (see help for %1 for details). "
                "There's no sense in executing this command explicitly. Instead just type the SQL query "
                "in the command prompt, without any command prefixed."
                ).arg(cmdName("use"));
}

bool CliCommandSql::isAsyncExecution() const
{
    return true;
}

void CliCommandSql::defineSyntax()
{
    syntax.setName("query");
    syntax.addArgument(STRING, tr("sql", "CLI command syntax"));
    syntax.setStrictArgumentCount(false);
}

void CliCommandSql::printResultsClassic(QueryExecutor* executor, SqlResultsPtr results)
{
    int rowIdColumns = executor->getRowIdResultColumns().size();
    int resultColumnCount = executor->getResultColumns().size();

    // Columns
    foreach (const QueryExecutor::ResultColumnPtr& resCol, executor->getResultColumns())
        qOut << resCol->displayName << "|";

    qOut << "\n";

    // Data
    SqlResultsRowPtr row;
    QList<QVariant> values;
    int i;
    while (results->hasNext())
    {
        row = results->next();
        i = 0;
        values = row->valueList().mid(rowIdColumns);
        foreach (QVariant value, values)
        {
            qOut << getValueString(value);
            if ((i + 1) < resultColumnCount)
                qOut << "|";

            i++;
        }

        qOut << "\n";
    }
    qOut.flush();
}

void CliCommandSql::printResultsFixed(QueryExecutor* executor, SqlResultsPtr results)
{
    QList<QueryExecutor::ResultColumnPtr> resultColumns = executor->getResultColumns();
    int resultColumnsCount = resultColumns.size();
    int rowIdColumns = executor->getRowIdResultColumns().size();
    int termCols = getCliColumns();
    int baseColWidth = termCols / resultColumns.size() - 1;

    if (resultColumnsCount == 0)
        return;

    if ((resultColumnsCount * 2 - 1) > termCols)
    {
        println(tr("Too many columns to display in %1 mode.").arg("FIXED"));
        return;
    }

    int width;
    QList<int> widths;
    for (int i = 0; i < resultColumnsCount; i++)
    {
        width = baseColWidth;
        if (i+1 == resultColumnsCount)
            width += (termCols - resultColumnsCount * (baseColWidth + 1) + 1);

        widths << width;
    }

    // Columns
    QStringList columns;
    foreach (const QueryExecutor::ResultColumnPtr& resCol, executor->getResultColumns())
        columns << resCol->displayName;

    printColumnHeader(widths, columns);

    // Data
    while (results->hasNext())
        printColumnDataRow(widths, results->next(), rowIdColumns);

    qOut.flush();
}

void CliCommandSql::printResultsColumns(QueryExecutor* executor, SqlResultsPtr results)
{
    // Check if we don't have more columns than we can display
    QList<QueryExecutor::ResultColumnPtr> resultColumns = executor->getResultColumns();
    int termCols = getCliColumns();
    int resultColumnsCount = resultColumns.size();
    QStringList headerNames;
    if (resultColumnsCount == 0)
        return;

    // Every column requires at least 1 character width + column separators between them
    if ((resultColumnsCount * 2 - 1) > termCols)
    {
        println(tr("Too many columns to display in %1 mode.").arg("COLUMNS"));
        return;
    }

    // Preload data (we will calculate column widths basing on real values)
    QList<SqlResultsRowPtr> allRows = results->getAll();
    int rowIdColumns = executor->getRowIdResultColumns().size();

    // Get widths of each column in every data row, remember the longest ones
    QList<SortedColumnWidth*> columnWidths;
    SortedColumnWidth* colWidth;
    foreach (const QueryExecutor::ResultColumnPtr& resCol, resultColumns)
    {
        colWidth = new SortedColumnWidth();
        colWidth->setHeaderWidth(resCol->displayName.length());
        columnWidths << colWidth;
        headerNames << resCol->displayName;
    }

    int dataLength;
    foreach (const SqlResultsRowPtr& row, allRows)
    {
        for (int i = 0; i < resultColumnsCount; i++)
        {
            dataLength = row->value(rowIdColumns + i).toString().length();
            columnWidths[i]->setMinDataWidth(dataLength);
        }
    }

    // Calculate width as it would be required to display entire rows
    int totalWidth = 0;
    foreach (colWidth, columnWidths)
        totalWidth += colWidth->getWidth();

    totalWidth += (resultColumnsCount - 1); // column separators

    // Adjust column sizes to fit into terminal window
    if (totalWidth < termCols)
    {
        // Expanding last column
        int diff = termCols - totalWidth;
        columnWidths.last()->incrWidth(diff);
    }
    else if (totalWidth > termCols)
    {
        // Shrinking columns
        shrinkColumns(columnWidths, termCols, resultColumnsCount, totalWidth);
    }

    // Printing
    QList<int> finalWidths;
    foreach (colWidth, columnWidths)
        finalWidths << colWidth->getWidth();

    printColumnHeader(finalWidths, headerNames);

    foreach (SqlResultsRowPtr row, allRows)
        printColumnDataRow(finalWidths, row, rowIdColumns);

    qOut.flush();
}

void CliCommandSql::printResultsRowByRow(QueryExecutor* executor, SqlResultsPtr results)
{
    // Columns
    int rowIdColumns = executor->getRowIdResultColumns().size();
    int colWidth = 0;
    foreach (const QueryExecutor::ResultColumnPtr& resCol, executor->getResultColumns())
    {
        if (resCol->displayName.length() > colWidth)
            colWidth = resCol->displayName.length();
    }

    QStringList columns;
    foreach (const QueryExecutor::ResultColumnPtr& resCol, executor->getResultColumns())
        columns << pad(resCol->displayName, -colWidth, ' ');

    // Data
    static const QString rowCntTemplate = tr("Row %1");
    int termWidth = getCliColumns();
    QString rowCntString;
    int i;
    int rowCnt = 1;
    SqlResultsRowPtr row;
    while (results->hasNext())
    {
        row = results->next();
        i = 0;
        rowCntString = " " + rowCntTemplate.arg(rowCnt) + " ";
        qOut << center(rowCntString, termWidth - 1, '-') << "\n";
        foreach (QVariant value, row->valueList().mid(rowIdColumns))
        {
            qOut << columns[i] + ": " + getValueString(value) << "\n";
            i++;
        }
        rowCnt++;
    }
    qOut.flush();
}

void CliCommandSql::shrinkColumns(QList<CliCommandSql::SortedColumnWidth*>& columnWidths, int termCols, int resultColumnsCount, int totalWidth)
{
    // This implements quite a smart shrinking algorithm:
    // All columns are sorted by their current total width (data and header width)
    // and then longest headers are shrinked first, then if headers are no longer a problem,
    // but the data is - then longest data values are shrinked.
    // If either the hader or the data value is huge (way more than fits into terminal),
    // then such column is shrinked in one step to a reasonable width, so it can be later
    // shrinked more precisely.
    int maxSingleColumnWidth = (termCols - (resultColumnsCount - 1) * 2 - 1);
    bool shrinkData;
    int previousTotalWidth = -1;
    while (totalWidth > termCols && totalWidth != previousTotalWidth)
    {
        shrinkData = true;
        previousTotalWidth = totalWidth;

        // Sort columns by current widths
        qSort(columnWidths);

        // See if we can shrink headers only, or we already need to shrink the data
        foreach (SortedColumnWidth* colWidth, columnWidths)
        {
            if (colWidth->isHeaderLonger())
            {
                shrinkData = false;
                break;
            }
        }

        // Do the shrinking
        if (shrinkData)
        {
            for (int i = resultColumnsCount - 1; i >= 0; i--)
            {
                // If the data is way larger then the terminal, shrink it to reasonable length in one step.
                // We also make sure that after data shrinking, the header didn't become longer than the data,
                // cause at this moment, we were finished with headers and we enforce shrinking data
                // and so do with headers.
                if (columnWidths[i]->getDataWidth() > maxSingleColumnWidth)
                {
                    totalWidth -= (columnWidths[i]->getDataWidth() - maxSingleColumnWidth);
                    columnWidths[i]->setDataWidth(maxSingleColumnWidth);
                    columnWidths[i]->setMaxHeaderWidth(maxSingleColumnWidth);
                    break;
                }
                else if (columnWidths[i]->getDataWidth() > 1) // just shrink it by 1
                {
                    totalWidth -= 1;
                    columnWidths[i]->decrDataWidth();
                    columnWidths[i]->setMaxHeaderWidth(columnWidths[i]->getDataWidth());
                    break;
                }
            }
        }
        else // shrinking headers
        {
            for (int i = resultColumnsCount - 1; i >= 0; i--)
            {
                // We will shrink only the header that
                if (!columnWidths[i]->isHeaderLonger())
                    continue;

                // If the header is way larger then the terminal, shrink it to reasonable length in one step
                if (columnWidths[i]->getHeaderWidth() > maxSingleColumnWidth)
                {
                    totalWidth -= (columnWidths[i]->getHeaderWidth() - maxSingleColumnWidth);
                    columnWidths[i]->setHeaderWidth(maxSingleColumnWidth);
                    break;
                }
                else if (columnWidths[i]->getHeaderWidth() > 1) // otherwise just shrink it by 1
                {
                    totalWidth -= 1;
                    columnWidths[i]->decrHeaderWidth();
                    break;
                }
            }
        }
    }

    if (totalWidth == previousTotalWidth && totalWidth > termCols)
        qWarning() << "The shrinking algorithm in printResultsColumns() failed, it could not shrink columns enough.";
}

void CliCommandSql::printColumnHeader(const QList<int>& widths, const QStringList& columns)
{
    QStringList line;
    int i = 0;
    foreach (const QString& col, columns)
    {
        line << pad(col.left(widths[i]), widths[i], ' ');
        i++;
    }

    qOut << line.join("|");

    line.clear();
    QString hline("-");
    for (i = 0; i < columns.count(); i++)
        line << hline.repeated(widths[i]);

    qOut << line.join("+");
}

void CliCommandSql::printColumnDataRow(const QList<int>& widths, const SqlResultsRowPtr& row, int rowIdOffset)
{
    int i = 0;
    QStringList line;
    foreach (const QVariant& value, row->valueList().mid(rowIdOffset))
    {
        line << pad(getValueString(value).left(widths[i]), widths[i], ' ');
        i++;
    }

    qOut << line.join("|");
}

QString CliCommandSql::getValueString(const QVariant& value)
{
    if (value.isValid() && !value.isNull())
        return value.toString();

    return CFG_CLI.Console.NullValue.get();
}

void CliCommandSql::executionFailed(int code, const QString& msg)
{
    UNUSED(code);
    qOut << tr("Query execution error: %1").arg(msg) << "\n\n";
    qOut.flush();
}

CliCommandSql::SortedColumnWidth::SortedColumnWidth()
{
    dataWidth = 0;
    headerWidth = 0;
    width = 0;
}

bool CliCommandSql::SortedColumnWidth::operator<(const CliCommandSql::SortedColumnWidth& other)
{
    return width < other.width;
}

int CliCommandSql::SortedColumnWidth::getHeaderWidth() const
{
    return headerWidth;
}

void CliCommandSql::SortedColumnWidth::setHeaderWidth(int value)
{
    headerWidth = value;
    updateWidth();
}

void CliCommandSql::SortedColumnWidth::setMaxHeaderWidth(int value)
{
    if (headerWidth > value)
    {
        headerWidth = value;
        updateWidth();
    }
}

void CliCommandSql::SortedColumnWidth::incrHeaderWidth(int value)
{
    headerWidth += value;
    updateWidth();
}

void CliCommandSql::SortedColumnWidth::decrHeaderWidth(int value)
{
    headerWidth -= value;
    updateWidth();
}

int CliCommandSql::SortedColumnWidth::getDataWidth() const
{
    return dataWidth;
}

void CliCommandSql::SortedColumnWidth::setDataWidth(int value)
{
    dataWidth = value;
    updateWidth();
}

void CliCommandSql::SortedColumnWidth::setMinDataWidth(int value)
{
    if (dataWidth < value)
    {
        dataWidth = value;
        updateWidth();
    }
}

void CliCommandSql::SortedColumnWidth::incrDataWidth(int value)
{
    dataWidth += value;
    updateWidth();
}

void CliCommandSql::SortedColumnWidth::decrDataWidth(int value)
{
    dataWidth -= value;
    updateWidth();
}

void CliCommandSql::SortedColumnWidth::incrWidth(int value)
{
    width += value;
    dataWidth = width;
    headerWidth = width;
}

int CliCommandSql::SortedColumnWidth::getWidth() const
{
    return width;
}

bool CliCommandSql::SortedColumnWidth::isHeaderLonger() const
{
    return headerWidth > dataWidth;
}

void CliCommandSql::SortedColumnWidth::updateWidth()
{
    width = qMax(headerWidth, dataWidth);
}
