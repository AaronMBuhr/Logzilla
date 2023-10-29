using System;
using System.Collections.Generic;
using System.Linq;
using System.Text;
using System.Threading.Tasks;

namespace SyslogAgent.Config
{
    public interface IValidatedOptionView : IOptionView
    {
        bool IsValid { get; set; }
    }
}
