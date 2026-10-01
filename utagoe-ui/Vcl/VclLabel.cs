// VCL TLabel の AutoSize / 右寄せ挙動を再現する label。
// VCL は taRightJustify + AutoSize で右端を固定して左へ伸びる。WinForms は左端固定なので、そのままだと値の桁数で trackbar 側へずれる。

namespace Utagoe.Vcl;

internal sealed class VclLabel : ThemedLabel
{
    private int? _right;

    public VclLabel(bool rightJustify)
    {
        AutoSize = true;
        RightJustify = rightJustify;
        if (rightJustify) TextAlign = ContentAlignment.TopRight;
    }

    public bool RightJustify { get; }

    protected override void SetBoundsCore(int x, int y, int width, int height, BoundsSpecified specified)
    {
        if (RightJustify)
        {
            if ((specified & BoundsSpecified.X) != 0)
            {
                // 明示配置時は先に右端位置を覚え、AutoSize 後の幅をそこへ合わせる。
                int right = x + ((specified & BoundsSpecified.Width) != 0 ? width : Width);
                _right = right;
                if (AutoSize)
                {
                    width = PreferredSize.Width;
                    x = right - width;
                    specified |= BoundsSpecified.Width;
                }
            }
            else if (_right is int right && width != Width)
            {
                // size だけ変わる AutoSize pass でも右端は固定する。
                x = right - width;
                specified |= BoundsSpecified.X;
            }
        }
        base.SetBoundsCore(x, y, width, height, specified);
    }
}
