c CLASS = S
c  
c  
c  This file is generated automatically by the setparams utility.
c  It sets the number of processors and the class of the NPB
c  in this directory. Do not modify it by hand.
c  

c full problem size
        integer isiz1, isiz2, isiz3
        parameter (isiz1=12, isiz2=12, isiz3=12)

c number of iterations and how often to print the norm
        integer itmax_default, inorm_default
        parameter (itmax_default=50, inorm_default=50)
        double precision dt_default
        parameter (dt_default = 0.5d0)
        logical  convertdouble
        parameter (convertdouble = .false.)
        character compiletime*11
        parameter (compiletime='30 Nov 2025')
        character npbversion*5
        parameter (npbversion='3.3.1')
        character cs1*3
        parameter (cs1='f77')
        character cs2*6
        parameter (cs2='$(F77)')
        character cs3*6
        parameter (cs3='(none)')
        character cs4*6
        parameter (cs4='(none)')
        character cs5*2
        parameter (cs5='-O')
        character cs6*2
        parameter (cs6='-O')
        character cs7*6
        parameter (cs7='randi8')
