using System;
using System.Globalization;
using System.Windows.Data;

namespace SyslogAgent.Config
{
    public class WidthConverter : IValueConverter
    {
        public object Convert( object value, Type targetType, object parameter, CultureInfo culture )
        {
            // Assuming value is the actual width of the StatusBar
            // Deduct the fixed width of the right TextBlock plus a margin
            double totalWidth = (double)value;
            const double rightTextBlockWidthPlusMargin = 100; // Assuming 80 width + 20 margin
            return totalWidth > rightTextBlockWidthPlusMargin ? totalWidth - rightTextBlockWidthPlusMargin : totalWidth;
        }

        public object ConvertBack( object value, Type targetType, object parameter, CultureInfo culture )
        {
            throw new NotImplementedException();
        }
    }
}
