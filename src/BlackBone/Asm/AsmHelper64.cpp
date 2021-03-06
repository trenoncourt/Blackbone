#include "AsmHelper64.h"

#include <assert.h>

namespace blackbone
{

AsmHelper64::AsmHelper64( )
    : _stackEnabled( true )
{
}

AsmHelper64::~AsmHelper64( void )
{
}

/// <summary>
/// Generate function prologue code
/// </summary>
/// <param name="switchMode">true if execution must be swithed to x64 mode</param>
void AsmHelper64::GenPrologue( bool switchMode /*= false*/ )
{
    // If switch is required, function was called from x86 mode,
    // so arguments can't be saved in x64 way, because there is no shadow space on the stack
    if (switchMode)
    {
        SwitchTo64();

        // Align stack
        _assembler.and_( asmjit::host::zsp, 0xFFFFFFFFFFFFFFF0 );
    }
    else
    {
        _assembler.mov( asmjit::host::qword_ptr( asmjit::host::rsp, 1 * WordSize ), asmjit::host::rcx );
        _assembler.mov( asmjit::host::qword_ptr( asmjit::host::rsp, 2 * WordSize ), asmjit::host::rdx );
        _assembler.mov( asmjit::host::qword_ptr( asmjit::host::rsp, 3 * WordSize ), asmjit::host::r8 );
        _assembler.mov( asmjit::host::qword_ptr( asmjit::host::rsp, 4 * WordSize ), asmjit::host::r9 );
    }
}

/// <summary>
/// Generate function epilogue code
/// </summary>
/// <param name="switchMode">true if execution must be swithed to x86 mode</param>
/// <param name="retSize">Stack change value</param>
void AsmHelper64::GenEpilogue( bool switchMode /*= false*/, int retSize /*= 0*/ )
{
    UNREFERENCED_PARAMETER( retSize );

    if (switchMode)
    {
        SwitchTo86();
    }
    else
    {
        _assembler.mov( asmjit::host::rcx, asmjit::host::qword_ptr( asmjit::host::rsp, 1 * WordSize ) );
        _assembler.mov( asmjit::host::rdx, asmjit::host::qword_ptr( asmjit::host::rsp, 2 * WordSize ) );
        _assembler.mov( asmjit::host::r8, asmjit::host::qword_ptr( asmjit::host::rsp, 3 * WordSize ) );
        _assembler.mov( asmjit::host::r9, asmjit::host::qword_ptr( asmjit::host::rsp, 4 * WordSize ) );
    }

    _assembler.ret();
}

/// <summary>
/// Generate function call
/// </summary>
/// <param name="pFN">Function pointer</param>
/// <param name="args">Function arguments</param>
/// <param name="cc">Ignored</param>
void AsmHelper64::GenCall( const AsmVariant& pFN, const std::vector<AsmVariant>& args, eCalligConvention /*cc = CC_stdcall*/ )
{
    //
    // reserve stack size (0x28 - minimal size for 4 registers and return address)
    // after call, stack must be aligned on 16 bytes boundary
    //
    size_t rsp_dif = (args.size() > 4) ? args.size() * WordSize : 0x28;

    // align on (16 bytes - sizeof(return address))
    rsp_dif = Align( rsp_dif, 0x10 );

    if (_stackEnabled)
        _assembler.sub( asmjit::host::rsp, rsp_dif + 8 );

    // Set args
    for (size_t i = 0; i < args.size(); i++)
        PushArg( args[i], i );

    if (pFN.type == AsmVariant::imm)
    {
        _assembler.mov( asmjit::host::rax, pFN.imm_val );
        _assembler.call( asmjit::host::rax );
    }
    else if (pFN.type == AsmVariant::reg)
    {
        _assembler.call( pFN.reg_val );
    }
    else
        assert("Invalid function pointer type" && false );

    if (_stackEnabled)
        _assembler.add( asmjit::host::rsp, rsp_dif + 8 );
}

/// <summary>
/// Save rax value and terminate current thread
/// </summary>
/// <param name="pExitThread">NtTerminateThread address</param>
/// <param name="resultPtr">Memry where rax value will be saved</param>
void AsmHelper64::ExitThreadWithStatus( uint64_t pExitThread, size_t resultPtr )
{
    if (resultPtr != 0)
    {
        _assembler.mov( asmjit::host::rdx, resultPtr );
        _assembler.mov( asmjit::host::dword_ptr( asmjit::host::rdx ), asmjit::host::rax );
    }

    _assembler.mov( asmjit::host::rdx, asmjit::host::rax );
    _assembler.mov( asmjit::host::rcx, 0 );
    _assembler.mov( asmjit::host::r13, pExitThread );
    _assembler.call( asmjit::host::r13 );
}

/// <summary>
/// Save return value and signal thread return event
/// </summary>
/// <param name="pSetEvent">NtSetEvent address</param>
/// <param name="ResultPtr">Result value memory location</param>
/// <param name="EventPtr">Event memory location</param>
/// <param name="errPtr">Error code memory location</param>
/// <param name="rtype">Return type</param>
void AsmHelper64::SaveRetValAndSignalEvent( size_t pSetEvent,
                                            size_t ResultPtr,
                                            size_t EventPtr, 
                                            size_t lastStatusPtr, 
                                            eReturnType rtype /*= rt_int32*/ )
{
    _assembler.mov( asmjit::host::rcx, ResultPtr );

    // FPU value has been already saved
    if (rtype == rt_int64 || rtype == rt_int32)
        _assembler.mov( asmjit::host::dword_ptr( asmjit::host::rcx ), asmjit::host::rax );

    // Save last NT status
    SetTebPtr();
    _assembler.add( asmjit::host::rdx, LAST_STATUS_OFS );
    _assembler.mov( asmjit::host::rdx, asmjit::host::dword_ptr( asmjit::host::rdx ) );
    _assembler.mov( asmjit::host::rax, lastStatusPtr );
    _assembler.mov( asmjit::host::dword_ptr( asmjit::host::rax ), asmjit::host::rdx );

    // NtSetEvent(hEvent, NULL)
    _assembler.mov( asmjit::host::rax, EventPtr );
    _assembler.mov( asmjit::host::rcx, asmjit::host::dword_ptr( asmjit::host::rax ) );
    _assembler.mov( asmjit::host::rdx, 0 );
    _assembler.mov( asmjit::host::r13, pSetEvent );
    _assembler.call( asmjit::host::r13 );
}


/// <summary>
/// Set stack reservation policy on call generation
/// </summary>
/// <param name="state">
/// If true - stack space will be reserved during each call generation
/// If false - no automatic stack reservation, user must allocate stack by hand
/// </param>
void AsmHelper64::EnableX64CallStack( bool state )
{
    _stackEnabled = state;
}

/// <summary>
/// Push function argument
/// </summary>
/// <param name="arg">Argument.</param>
/// <param name="regidx">Push type(register or stack)</param>
void AsmHelper64::PushArg( const AsmVariant& arg, size_t index )
{
    switch (arg.type)
    {

    case AsmVariant::imm:
    case AsmVariant::structRet:
        PushArgp( arg.imm_val, index );
        break;

    case AsmVariant::dataPtr:
    case AsmVariant::dataStruct:
        PushArgp( arg.new_imm_val, index );
        break;

    case AsmVariant::imm_double:        
        PushArgp( arg.getImm_double(), index, true );
        break;

    case AsmVariant::imm_float:
        PushArgp( arg.getImm_float(), index, true );
        break;

    case AsmVariant::mem_ptr:
        _assembler.lea( asmjit::host::rax, arg.mem_val );
        PushArgp( asmjit::host::rax, index );
        break;

    case AsmVariant::mem:
        PushArgp( arg.mem_val, index );
        break;

    case AsmVariant::reg:
        PushArgp( arg.reg_val, index );
        break;

    default:
        assert( "Invalid argument type" && false );
        break;
    }
}

/// <summary>
/// Push function argument
/// </summary>
/// <param name="arg">Argument</param>
/// <param name="index">Argument index</param>
/// <param name="fpu">true if argument is a floating point value</param>
template<typename _Type>
void AsmHelper64::PushArgp( const _Type& arg, size_t index, bool fpu /*= false*/ )
{
    static const asmjit::host::GpReg regs[] = { asmjit::host::rcx, asmjit::host::rdx, asmjit::host::r8, asmjit::host::r9 };
    static const asmjit::host::XmmReg xregs[] = { asmjit::host::xmm0, asmjit::host::xmm1, asmjit::host::xmm2, asmjit::host::xmm3 };

    // Pass via register
    if (index < 4)
    {
        // Use XMM register
        if (fpu)
        {
            _assembler.mov( asmjit::host::rax, arg );
            _assembler.movq( xregs[index], asmjit::host::rax );
        }
        else
            _assembler.mov( regs[index], arg );
    }
    // Pass on stack
    else
    {
        _assembler.mov( asmjit::host::rax, arg );
        _assembler.mov( asmjit::host::qword_ptr( asmjit::host::rsp, static_cast<int32_t>(index)* WordSize ), asmjit::host::rax );
    }
}

}
