#include "lookup_table1d.h"
#include <algorithm>
#include <limits>

using std::size_t;

// Set table values to new input values, validate first then set value in.
LookupTable::SetState LookupTable1D::SetTable(const Eigen::RowVectorXd &x_table, const Eigen::RowVectorXd &y_table)
{
    if (CheckTableState(x_table, y_table) == TableState::valid)
    {
        x_table_ = x_table;
        y_table_ = y_table;
        RefreshTableState();
        return SetState::success;
    }
    else
    {
        // if the input value is invalid, check the current table value to set the table_state flag
        table_valid_ = (CheckTableState(x_table_, y_table_) == TableState::valid);
        return table_valid_? SetState::remain : SetState::fail;
    }
}
LookupTable::SetState LookupTable1D::SetTable(const std::vector<double> &x_vec, const std::vector<double> &y_vec)
{
    // this is vector edition, first convert then call the base SetTable function.
    Eigen::RowVectorXd x_table = Eigen::Map<const Eigen::RowVectorXd>(x_vec.data(), x_vec.size());
    Eigen::RowVectorXd y_table = Eigen::Map<const Eigen::RowVectorXd>(y_vec.data(), y_vec.size());
    return SetTable(x_table, y_table);
}

bool LookupTable1D::ClearTable()
{
    x_table_.resize(0);
    y_table_.resize(0);
    table_empty_ = true;
    table_size_ = 0;
    return true;
}

void LookupTable1D::RefreshTableState()
{
    table_valid_ = (CheckTableState(x_table_, y_table_) == TableState::valid);
    if (table_valid_)
    {
        table_empty_ = false;
        table_size_ = x_table_.size();
    }
    else if(!table_empty_)
    {
        bool cleared = ClearTable();    // clear the table if invalid but not empty
    }
}

LookupTable1D::TableState LookupTable1D::CheckTableState(const Eigen::RowVectorXd &input_vector1, const Eigen::RowVectorXd &input_vector2)
{
    if (input_vector1.size() == 0 || input_vector2.size() == 0)
    {
        return TableState::empty;
    }
    else if (input_vector1.size() > max_table_size_ || input_vector1.size() < 2)
    {
        return TableState::size_invalid; // x table size must be within the range of [2 max_table_size]
    }
    else if (input_vector1.size() != input_vector2.size())
    {
        return TableState::size_not_match; // y table size must be equal to x table size
    }
    else if (!isStrictlyIncreasing(input_vector1))
    {
        return TableState::x_not_increase; // x table data must be strictly increasing
    }
    else
    {
        return TableState::valid; // valid data for assignment
    }
}


// Configurations of lookup methods
void LookupTable1D::SetExtrapMethod(const ExtrapMethod &method)
{
    extrap_method_ = method;
    if (table_valid_)
    {
        lower_extrap_value_specify_ = y_table_(0);
        upper_extrap_value_specify_ = y_table_(y_table_.size() - 1);
    }
}
void LookupTable1D::SetExtrapMethod(const ExtrapMethod &method, const double &lower_value, const double &upper_value)
{
    extrap_method_ = method;
    lower_extrap_value_specify_ = lower_value;
    upper_extrap_value_specify_ = upper_value;
}

// Prelook
std::size_t LookupTable1D::PreLookup(const double &x_value)
{
    size_t prelook_index = SearchIndex(x_value, x_table_, search_method_, prelook_index_);
    if (prelook_index >= 0 && prelook_index <= x_table_.size()) // valid prelookup
    {
        prelook_index_ = prelook_index; // store value
    }
    else
    {
        prelook_index = prelook_index_; // keep last value for invalid prelookup
    }
    return prelook_index;
}

// Interpolation between the two closest points
double LookupTable1D::Interpolation(const std::size_t &index, const double &x_value)
{
    switch (interp_method_)
    {
    case InterpMethod::linear:
        return InterpolationLinear(index, x_value);

    case InterpMethod::nearest:
        return InterpolationNearest(index, x_value);

    case InterpMethod::next:
        return InterpolationNext(index, x_value);

    case InterpMethod::previous:
        return InterpolationPrevious(index, x_value);

    default:
        RefreshTableState();
        return lookup_result_;
    }
}
double LookupTable1D::InterpolationLinear(const std::size_t &index, const double &x_value)
{
    // Retrieve the two nearest points for interpolation
    double x1 = x_table_[index - 1];
    double x2 = x_table_[index];
    double y1 = y_table_[index - 1];
    double y2 = y_table_[index];

    // Calculate the weight for linear interpolation
    bool equal_zero = std::abs(x2 - x1) < epsilon_;
    double weight = equal_zero ? 0.5 : (x_value - x1) / (x2 - x1);

    // Linearly interpolate the y value based on the weight
    return y1 + weight * (y2 - y1);
}
double LookupTable1D::InterpolationNearest(const std::size_t &index, const double &x_value)
{
    return ((x_value - x_table_[index - 1]) <= (x_table_[index] - x_value)) ? y_table_[index - 1] : y_table_[index];
}
double LookupTable1D::InterpolationNext(const std::size_t &index, const double &x_value)
{
    return y_table_[index];
}
double LookupTable1D::InterpolationPrevious(const std::size_t &index, const double &x_value)
{
    return y_table_[index - 1];
}

// Extrapolation if input is out of bounds
double LookupTable1D::Extrapolation(const std::size_t &index, const double &x_value)
{
    switch (extrap_method_)
    {
    case ExtrapMethod::clip:
        return ExtrapolationClip(index);
    case ExtrapMethod::linear:
        return ExtrapolationLinear(index, x_value);
    case ExtrapMethod::specify:
        return ExtrapolationSpecify(index, lower_extrap_value_specify_, upper_extrap_value_specify_);
    default:
        RefreshTableState();
        return lookup_result_;
    }
}
double LookupTable1D::ExtrapolationClip(const std::size_t &index)
{
    if (index == 0)
    {
        return y_table_(0);
    }
    else if (index == table_size_)
    {
        return y_table_(y_table_.size() - 1);
    }
    else
    {
        return lookup_result_; // if failure occurs, output the last value.
    }
}
double LookupTable1D::ExtrapolationLinear(const std::size_t &index, const double &x_value)
{
    if (index == 0)
    {
        double x1 = x_table_[0];
        double x2 = x_table_[1];
        double y1 = y_table_[0];
        double y2 = y_table_[1];

        // Calculate the weight for linear extrapolation
        bool equal_zero = std::abs(x2 - x1) < epsilon_;
        double weight = equal_zero ? 0.5 : (x_value - x1) / (x2 - x1);

        // Linearly extrapolate the y value based on the weight
        return y1 + weight * (y2 - y1);
    }
    else if (index == table_size_)
    {
        double x1 = x_table_[table_size_ - 2];
        double x2 = x_table_[table_size_ - 1];
        double y1 = y_table_[table_size_ - 2];
        double y2 = y_table_[table_size_ - 1];

        // Calculate the weight for linear extrapolation
        bool equal_zero = std::abs(x2 - x1) < epsilon_;
        double weight = equal_zero ? 0.5 : (x_value - x1) / (x2 - x1);

        // Linearly extrapolate the y value based on the weight
        return y1 + weight * (y2 - y1);
    }
    else
    {
        return lookup_result_; // if failure occurs, output the last value.
    }
}
double LookupTable1D::ExtrapolationSpecify(const std::size_t &index, const double &lower_extrap_value, const double &upper_extrap_value)
{
    if (index == 0)
    {
        return lower_extrap_value;
    }
    else if (index == table_size_)
    {
        return upper_extrap_value;
    }
    else
    {
        return lookup_result_; // if failure occurs, output the last value.
    }
}

// Final function LookupTable
double LookupTable1D::LookupTable(const double &x_value)
{
    if (table_valid_)
    {
        size_t index = PreLookup(x_value);
        if (index == 0 || index == table_size_) // in the case for extrapolation
        {
            lookup_result_ = Extrapolation(index, x_value);
        }
        else
        {
            lookup_result_ = Interpolation(index, x_value);
        }
        x_value_ = x_value; // restore the input value to member variable, for use in some cases.
    }
    else
    {
        RefreshTableState();
    }
    return lookup_result_;
}