package org.flxtr.android;
import android.app.Activity;
import android.os.Bundle;
public final class TvPairActivity extends Activity {
    @Override public void onCreate(Bundle state){super.onCreate(state);String address=getIntent().getData()==null?null:getIntent().getData().getQueryParameter("url");TvHandoff.pair(this,address,this::finish);}
}
