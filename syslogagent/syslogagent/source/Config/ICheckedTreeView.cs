using System;
using System.Collections.Generic;
using System.Linq;
using System.Text;
using System.Threading.Tasks;

namespace SyslogAgent.Config
{
    public interface ICheckedTreeView<T>
    {
        List<T> GetMembers();
    }
}
