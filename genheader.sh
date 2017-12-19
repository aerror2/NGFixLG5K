
ot=$1
while read line; do  

symbol=`echo "$line" | awk -F"//" '{print $1}' | tr -d ' '`
funcdef=`echo "$line" | awk -F"//" '{print $2}'`
funcname=`echo "$funcdef"  | sed 's/\([^)()]*\)(\(.*\)/\1/g'|sed 's/::/_/g' | tr -d ' ' `
funcparam=`echo "$funcdef" | sed 's/\([^)()]*\)(\(.*\)/\2/g'`

org_funcname="org_$funcname"
func_type="t_$funcname"


if [ "$ot" == "head" ]; then
	echo "using $func_type=IOReturn (*) (IONDRVFramebuffer *that,$funcparam ;"
	echo "$func_type $org_funcname={nullptr};"
	echo "static IOReturn $funcname(IONDRVFramebuffer *that,$funcparam ;"
elif [ "$ot" == "patch" ]; then

	echo  "method_address = patcher.solveSymbol(index, \"$symbol\");"
	echo  "if (method_address) {"
	echo   "DBGLOG(\"ngfx\", \"obtained $symbol\");"
	echo   "patcher.clearError();"
	echo   "$org_funcname = reinterpret_cast<$func_type>(patcher.routeFunction(method_address, reinterpret_cast<mach_vm_address_t>($funcname), true));"
	echo   "}"
	echo   "else"
	echo   "{"
	echo   "SYSLOG(\"ngfx\", \"failed to resolve $symbol\");"
	echo   "}"
else
	echo  "IOReturn NGFX::$funcname(IONDRVFramebuffer *that,$funcparam "
	echo "{"
	echo "if (callbackNGFX && callbackNGFX->$org_funcname)"
	echo "{"
	echo "DBGLOG(\"ngfx\", \"$funcname %s:%s begin \",that->getName(),that->getProvider()!=NULL?that->getProvider()->getName():\"nopriver\");"
	echo "IOReturn ret = callbackNGFX->$org_funcname(that,);"
	echo "DBGLOG(\"ngfx\", \"$funcname %s:%s ret %x \",that->getName(), that->getProvider()!=NULL?that->getProvider()->getName():\"nopriver\",ret);"
	echo "return ret;"
	echo "}"
	echo "return kIOReturnSuccess;"
	echo "}"
fi

done  | sed -E 's/,[ /t]*)/)/g'